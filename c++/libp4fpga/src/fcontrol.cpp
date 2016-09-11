/*
  Copyright 2015-2016 P4FPGA Project

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0


  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "fcontrol.h"
#include "table.h"
#include "action.h"
#include "fstruct.h"
#include "funion.h"
#include "codegeninspector.h"
#include "string_utils.h"
#include "vector_utils.h"

namespace FPGA {

using namespace Control;

class ExpressionConverter : public Inspector {
 public:
  cstring bsv = "";
  explicit ExpressionConverter () {}
  bool preorder(const IR::MethodCallExpression* expr){
    auto m = expr->method->to<IR::Member>();
    if (m->member == "isValid") {
      bsv += cstring("isValid(") + m->expr->toString() + cstring(")");
    }
    return false;
  }
  bool preorder(const IR::Grt* expr) {
    bsv += cstring("(");
    visit(expr->left);
    bsv += cstring(" > ");
    visit(expr->right);
    bsv += cstring(")");
    return false;
  }
  bool preorder(const IR::LAnd* expr) {
    bsv += cstring("(");
    visit(expr->left);
    bsv += cstring(" && ");
    visit(expr->right);
    bsv += cstring(")");
    return false;
  }
  bool preorder(const IR::Constant* cst) {
    bsv += cstring(cst->toString());
    return false;
  }
  bool preorder(const IR::Member* expr) {
    bsv += expr->toString();
    return false;
  }
};

bool FPGAControl::build() {
  const IR::P4Control* cont = controlBlock->container;
  LOG1("Processing " << cont);
  cfg = new CFG();
  cfg->build(cont, program->refMap, program->typeMap);

  cfg->dbprint(std::cout);
  if (cfg->entryPoint->successors.size() == 0) {
    LOG1("init table null");
  } else {
    BUG_CHECK(cfg->entryPoint->successors.size() == 1, "Expected 1 start node for %1%", cont);
    auto start = (*(cfg->entryPoint->successors.edges.begin()))->endpoint;
    LOG1(start);
  }

  // build map <cstring, IR::P4Action*>
  for (auto s : *controlBlock->container->getDeclarations()) {
    if (s->is<IR::P4Action>()) {
      auto action = s->to<IR::P4Action>();
      auto name = nameFromAnnotation(action->annotations, action->name);
      basicBlock.emplace(name, action);
    }
  }

  // constantValue : compile-time allocated resource, such as table..
  // build map <cstring, IR::P4Table*>
  for (auto c : controlBlock->constantValue) {
    auto b = c.second;
    if (!b->is<IR::Block>()) continue;
    if (b->is<IR::TableBlock>()) {
      auto tblblk = b->to<IR::TableBlock>();
      auto table = tblblk->container;
      auto name = nameFromAnnotation(table->annotations, table->name);
      tables.emplace(name, table);

      // populate map <Action, Table>
      for (auto act : *table->getActionList()->actionList) {
        auto element = act->to<IR::ActionListElement>();
        if (element->expression->is<IR::PathExpression>()) {
          //LOG1("Path " << element->expression->to<IR::PathExpression>());
        } else if (element->expression->is<IR::MethodCallExpression>()) {
          auto expression = element->expression->to<IR::MethodCallExpression>();
          auto type = program->typeMap->getType(expression->method, true);
          auto action = expression->method->toString();
          action_to_table[action] = table;
          LOG1("action yyy" << action);
        }
      }

      // populate map <Metadata, Table>
      auto keys = table->getKey();
      if (keys == nullptr) return false;

      for (auto key : *keys->keyElements) {
        auto element = key->to<IR::KeyElement>();
        if (element->expression->is<IR::Member>()) {
          auto m = element->expression->to<IR::Member>();
          auto type = program->typeMap->getType(m->expr, true);
          // from meta
          if (type->is<IR::Type_Struct>()) {
            auto t = type->to<IR::Type_StructLike>();
            auto f = t->getField(m->member);
            metadata_to_table[f].insert(table);
          // from hdr
          } else if (type->is<IR::Type_Header>()) {
            auto t = type->to<IR::Type_StructLike>();
            auto f = t->getField(m->member);
            metadata_to_table[f].insert(table);
          }
        }
      }
    } else if (b->is<IR::ExternBlock>()) {
      auto ctrblk = b->to<IR::ExternBlock>();
      LOG1("extern " << ctrblk);
    } else {
      ::error("Unexpected block %s nested within control", b->toString());
    }
  }

  for (auto n : metadata_to_action) {
    // LOG1("action " << n.first << action_to_table[n.second]);
    metadata_to_table[n.first].insert(action_to_table[n.second]);
  }

  for (auto n : metadata_to_table) {
    LOG1("metadata " << n.first << n.second);
    for (auto n1 : n.second) {
      for (auto n2 : n.second) {
        if (n1 == n2) continue;
        std::pair<const IR::StructField*, const IR::P4Table*> p (n.first, n2);
        adj_list[n1].insert(p);
      }
    }
  }
  return true;
}

void FPGAControl::emitEntryRule(BSVProgram & bsv, const CFG::Node* node) {
  append_format(bsv, "rule rl_entry if (entry_req_ff.notEmpty);");
  incr_indent(bsv);
  append_line(bsv, "entry_req_ff.deq;");
  append_line(bsv, "let _req = entry_req_ff.first;");
  append_line(bsv, "let meta = _req.meta;");
  append_line(bsv, "let pkt = _req.pkt;");
  append_line(bsv, "MetadataRequest req = MetadataRequest {pkt: pkt, meta: meta};");
  if (tables.size() == 0) {
    append_line(bsv, "exit_req_ff.enq(req);");
  } else {
    BUG_CHECK(node->successors.size() == 1, "Expected 1 start node for %1%", node);
    auto start = (*(node->successors.edges.begin()))->endpoint;
    append_format(bsv, "%s_req_ff.enq(req);", start->name);
  }
  decr_indent(bsv);
  append_line(bsv, "endrule");
}

void FPGAControl::emitExitRule(BSVProgram & bsv, const CFG::Node* node) {
  append_format(bsv, "rule rl_exit if (exit_req_ff.notEmpty);");
  incr_indent(bsv);
  append_line(bsv, "exit_req_ff.deq;");
  decr_indent(bsv);
  append_line(bsv, "endrule");
}

void FPGAControl::emitTableRule(BSVProgram & bsv, const CFG::TableNode* node) {
  auto table = node->table->to<IR::P4Table>();
  auto name = nameFromAnnotation(table->annotations, table->name);
  auto type = CamelCase(name);
  append_format(bsv, "rule rl_%s if (%s_rsp_ff.notEmpty);", name, name);
  incr_indent(bsv);
  append_format(bsv, "%s_rsp_ff.deq;", name);
  append_format(bsv, "let _rsp = %s_rsp_ff.first;", name);
  // find next states
  append_line(bsv, "let meta = _rsp.meta;");
  append_line(bsv, "let pkt = _rsp.pkt;");
  append_line(bsv, "case (_rsp) matches");
  incr_indent(bsv);
  for (auto s : node->successors.edges) {
    if (s->label == nullptr) {
      append_line(bsv, "default: begin");
      incr_indent(bsv);
      append_line(bsv, "MetadataRequest req = MetadataRequest { pkt : pkt, meta : meta};");
      append_line(bsv, "%s_req_ff.enq(req);", s->endpoint->name);
      decr_indent(bsv);
      append_line(bsv, "end");
    } else {
      append_line(bsv, "%s: begin", s->label);
      incr_indent(bsv);
      append_line(bsv, "MetadataRequest req = MetadataRequest { pkt : pkt, meta : meta};");
      append_line(bsv, "%s_req_ff.enq(req);", s->endpoint->name);
      decr_indent(bsv);
      append_line(bsv, "end");
    }
  }
  decr_indent(bsv);
  append_line(bsv, "endcase");
  decr_indent(bsv);
  append_line(bsv, "endrule");
}

void FPGAControl::emitCondRule(BSVProgram & bsv, const CFG::IfNode* node) {
  auto sig = cstring("w_") + node->name;
  auto name = node->name;
  append_format(bsv, "rule rl_%s if (%s);", node->name, sig);
  incr_indent(bsv);
  auto stmt = node->statement->to<IR::IfStatement>();
  // LOG1(node << " succ " << node->successors.edges);
  append_format(bsv, "%s_req_ff.deq;", name);
  append_format(bsv, "let _req = %s_req_ff.first;", name);

  auto ifTrue = cstring("");
  auto ifFalse = cstring("");
  for (auto e : node->successors.edges) {
    if (e->isBool()) {
      if (e->getBool()) {
        ifTrue = e->getNode()->name + cstring("_req_ff.enq(_req);");
      } else {
        ifFalse = e->getNode()->name + cstring("_req_ff.enq(_req);");
      }
    }
  }
  ExpressionConverter visitor;
  stmt->condition->apply(visitor);
  if (ifTrue != "") {
    append_format(bsv, "if (%s) begin", visitor.bsv);
    incr_indent(bsv);
    append_format(bsv, ifTrue);
    decr_indent(bsv);
    append_line(bsv, "end");
  }
  if (ifFalse != "") {
    append_line(bsv, "else begin");
    incr_indent(bsv);
    append_format(bsv, ifFalse);
    decr_indent(bsv);
    append_line(bsv, "end");
  }
  decr_indent(bsv);
  append_line(bsv, "endrule");
}

void FPGAControl::emitDeclaration(BSVProgram & bsv) {
  // basic block instances
  for (auto b : basicBlock) {
    auto name = nameFromAnnotation(b.second->annotations, b.second->name);
    auto type = CamelCase(name);
    auto camelCaseName = camelCase(name);
    append_format(bsv, "Control::%s %s <- mk%s();", type, camelCaseName, type);
  }
  for (auto t : tables) {
    auto table = t.second->to<IR::P4Table>();
    if (table == nullptr)
      continue;
    auto name = nameFromAnnotation(table->annotations, table->name);
    auto type = CamelCase(name);
    append_format(bsv, "Control::%s %s <- mk%s();", type, name, type);
  }
}

void FPGAControl::emitFifo(BSVProgram & bsv) {
  append_line(bsv, "FIFOF#(MetadataRequest) entry_req_ff <- mkFIFOF;");
  append_line(bsv, "FIFOF#(MetadataResponse) entry_rsp_ff <- mkFIFOF;");
  for (auto t : tables) {
    auto table = t.second->to<IR::P4Table>();
    auto name = nameFromAnnotation(table->annotations, table->name);
    auto type = CamelCase(name);
    append_line(bsv, "FIFOF#(MetadataRequest) %s_req_ff <- mkFIFOF;", name);
    append_line(bsv, "FIFOF#(MetadataResponse) %s_rsp_ff <- mkFIFOF;", name);
  }
  append_line(bsv, "FIFOF#(MetadataRequest) exit_req_ff <- mkFIFOF;");
  append_line(bsv, "FIFOF#(MetadataResponse) exit_rsp_ff <- mkFIFOF;");
}

void FPGAControl::emitConnection(BSVProgram & bsv) {
  // table to fifo
  for (auto t : tables) {
    auto table = t.second->to<IR::P4Table>();
    auto name = nameFromAnnotation(table->annotations, table->name);
    auto type = CamelCase(name);
    append_format(bsv, "mkConnection(toClient(%s_req_ff, %s_rsp_ff), %s.prev_control_state);", name, name, name);

    auto actionList = table->getActionList()->actionList;
    int idx = 0;
    for (auto action : *actionList) {
      auto elem = action->to<IR::ActionListElement>();
      if (elem->expression->is<IR::MethodCallExpression>()) {
        auto e = elem->expression->to<IR::MethodCallExpression>();
        auto t = program->typeMap->getType(e->method, true);
        auto n = e->method->toString();
        append_format(bsv, "mkChan(mkFIFOF, mkFIFOF, %s.next_control_state_%d, %s.prev_control_state);", name, idx, camelCase(n));
        idx ++ ;
      }
    }
  }
}

void FPGAControl::emitDebugPrint(BSVProgram & bsv) {
  append_line(bsv, "Reg#(int) cf_verbosity <- mkConfigRegU;");
  append_line(bsv, "function Action dbprint(Integer level, Fmt msg);");
  incr_indent(bsv);
  append_line(bsv, "action");
  append_line(bsv, "if (cf_verbosity > fromInteger(level)) begin");
  incr_indent(bsv);
  append_line(bsv, "$display(\"(%%0d) \" , $time, msg);");
  decr_indent(bsv);
  append_line(bsv, "end");
  append_line(bsv, "endaction");
  decr_indent(bsv);
  append_line(bsv, "endfunction");
}

void FPGAControl::emitTables(BSVProgram & bsv) {
  for (auto t : tables) {
    // LOG1("emit Tables");
    TableCodeGen visitor(this, bsv);
    t.second->apply(visitor);
  }
}


void FPGAControl::emitActions(BSVProgram & bsv) {
  for (auto b : basicBlock) {
    ActionCodeGen visitor(this, bsv);
    b.second->apply(visitor);
    auto stmt = b.second->body->to<IR::BlockStatement>();
    if (stmt == nullptr) continue;
    // encode to cpu instruction
    //for (auto path : *stmt->components) {
    //  path->apply(visitor);
    //}
  }
}

void FPGAControl::emitActionTypes(BSVProgram & bsv) {
  UnionCodeGen visitor(this, bsv);
  visitor.emit();
  for (auto b : tables) {
    UnionCodeGen visitor(this, bsv);
    b.second->apply(visitor);
  }
}


// control block module
void FPGAControl::emit(BSVProgram & bsv) {
  auto cbname = controlBlock->container->name.toString();
  auto cbtype = CamelCase(cbname);

  append_line(bsv, "import StructDefines::*;");
  append_line(bsv, "import UnionDefines::*;");
  append_line(bsv, "import CPU::*;");
  append_line(bsv, "import IMem::*;");
  append_line(bsv, "import List::*;");
  emitTables(bsv);
  emitActions(bsv);
  emitActionTypes(bsv);

  // TODO: synthesize boundary
  append_format(bsv, "// =============== control %s ==============", cbname);
  append_line(bsv, "interface %s;", cbtype);
  incr_indent(bsv);
  append_line(bsv, "interface Client#(MetadataRequest, MetadataResponse) next;");
  append_line(bsv, "interface MemFreeClient freeClient;");
  append_line(bsv, "method Action set_verbosity(int verbosity);");
  decr_indent(bsv);
  append_line(bsv, "endinterface");
  append_line(bsv, "module mk%s #(Vector#(numClients, Client#(MetadataRequest, MetadataResponse)) mdc) (%s);", cbtype, cbtype);
  incr_indent(bsv);
  emitDebugPrint(bsv);
  emitDeclaration(bsv);
  emitFifo(bsv);
  append_line(bsv, "Vector#(numClients, Server#(MetadataRequest, MetadataResponse)) mds = replicate(toServer(entry_req_ff, entry_rsp_ff));");
  append_line(bsv, "mkConnection(mds, mdc);");
  emitConnection(bsv);

  if (cfg != nullptr) {
    if (cfg->entryPoint != nullptr) {
      emitEntryRule(bsv, cfg->entryPoint);
    }
    for (auto node : cfg->allNodes) {
      if (node->is<CFG::TableNode>()) {
        auto t = node->to<CFG::TableNode>();
        emitTableRule(bsv, t);
      } else if (node->is<CFG::IfNode>()) {
        auto n = node->to<CFG::IfNode>();
        emitCondRule(bsv, n);
      }
    }
    if (cfg->exitPoint != nullptr) {
      emitExitRule(bsv, cfg->exitPoint);
    }
  }
  decr_indent(bsv);
  append_line(bsv, "endmodule");
}

}  // namespace FPGA
