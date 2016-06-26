# Copyright 2016 Han Wang
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import lib.ast as ast
from lib.utils import CamelCase
import primitives as prm

def get_reg_array_size(name, json_dict):
    for array in json_dict['register_arrays']:
        if array['name'] == name:
            bitwidth = array['bitwidth']
            size = array['size']
            return bitwidth, size


class Primitive(object):
    """
    base class for primitive
    """
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self): return []
    def buildFFs(self): return []
    def buildTXRX(self, json_dict): return []
    def buildInterface(self, json_dict): return []
    def buildInterfaceDef(self): return []

    def isRegRead(self): return False
    def isRegWrite(self): return False

class ModifyField(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class RegisterRead(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def isRegRead(self):
        return True

    def build(self):
        stmt = []
        return stmt

    def buildReadRequest(self):
        TMP1 = "let %(name)s_req = %(type)sReqT { addr: %(addr)s, data: ?, write: False };"
        TMP2 = "tx_info_%(name)s.enq(%(name)s_req);"
        name = self.parameters[1]['value']
        ptype = CamelCase(name) #FIXME
        if type(self.parameters[2]['value']) is list:
            addr = self.parameters[2]['value'][1]
        else:
            addr = self.parameters[2]['value'][0]
        stmt = []
        stmt.append(ast.Template(TMP1, {"name": name, "type": ptype, "addr": addr}))
        stmt.append(ast.Template(TMP2, {"name": name}))
        return stmt

    def buildReadResponse(self):
        TMP1 = "let %(name)s = rx_info_%(name)s.get;"
        name = self.parameters[0]['value'][1]
        stmt = []
        stmt.append(ast.Template(TMP1, {"name": name}))
        return stmt

    def buildTXRX(self, json_dict):
        TMP1 = "RX #(RegRequest#(%(asz)s, %(dsz)s)) rx_%(name)s <- mkRX;"
        TMP2 = "TX #(RegResponse#(%(dsz)s)) tx_%(name)s <- mkTX;"
        TMP3 = "let rx_info_%(name)s = rx_%(name)s.u;"
        TMP4 = "let tx_info_%(name)s = tx_%(name)s.u;"
        stmt = []
        name = self.parameters[1]['value']
        dsz, asz= get_reg_array_size(name, json_dict)
        pdict = {'name': name, 'asz': asz, 'dsz': dsz}
        stmt.append(ast.Template(TMP1, pdict))
        stmt.append(ast.Template(TMP2, pdict))
        stmt.append(ast.Template(TMP3, pdict))
        stmt.append(ast.Template(TMP4, pdict))
        return stmt

    def buildInterface(self, json_dict):
        TMP1 = "Client#(RegRequest#(%(asz)s, %(dsz)s), RegResponse#(%(dsz)s))"
        stmt = []
        name = self.parameters[1]['value']
        tname = self.parameters[0]['value'][1]
        ptype = CamelCase(tname)
        dsz, asz= get_reg_array_size(name, json_dict)
        pdict = {'name': name, 'asz': asz, 'dsz': dsz}
        intf = ast.Interface(tname, typeDefType = TMP1 % pdict)
        stmt.append(intf)
        return stmt

    def buildInterfaceDef(self):
        TMP1 = "interface %(name)s = toClient #(tx_info_%(name)s.e, rx_info_%(name)s.e);"
        stmt = []
        name = self.parameters[1]['value']
        tname = self.parameters[0]['value'][1]
        ptype = CamelCase(name)
        pdict = {"name": name, "tname": tname}
        stmt.append(ast.Template(TMP1, pdict))
        return stmt

class RegisterWrite(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def isRegWrite(self):
        return True

    def build(self):
        stmt = []
        return stmt

    def buildWriteRequest(self):
        TMP1 = "let %(name)s_req = %(type)sReqT { addr: %(addr)s, data: %(data)s, write: True };"
        TMP2 = "tx_info_%(name)s.enq(%(name)s_req);"
        name = self.parameters[0]['value']
        ptype = CamelCase(name)
        if type(self.parameters[1]['value']) is list:
            addr = self.parameters[1]['value'][1]
        else:
            addr = self.parameters[1]['value'][0]
        if type(self.parameters[2]['value']) is list:
            data = self.parameters[2]['value'][1]
        else:
            data = self.parameters[2]['value'][0]
        stmt = []
        stmt.append(ast.Template(TMP1, {"name": name, "type": ptype, "addr": addr, "data": data}))
        stmt.append(ast.Template(TMP2, {"name": name}))
        return stmt

    def buildTXRX(self, json_dict):
        TMP1 = "RX #(RegRequest#(%(asz)s, %(dsz)s)) rx_%(name)s <- mkRX;"
        TMP2 = "TX #(RegResponse#(%(dsz)s)) tx_%(name)s <- mkTX;"
        TMP3 = "let rx_info_%(name)s = rx_%(name)s.u;"
        TMP4 = "let tx_info_%(name)s = tx_%(name)s.u;"
        stmt = []
        name = self.parameters[0]['value']
        dsz, asz= get_reg_array_size(name, json_dict)
        pdict = {'name': name, 'asz': asz, 'dsz': dsz}
        stmt.append(ast.Template(TMP1, pdict))
        stmt.append(ast.Template(TMP2, pdict))
        stmt.append(ast.Template(TMP3, pdict))
        stmt.append(ast.Template(TMP4, pdict))
        return stmt

    def buildInterface(self, json_dict):
        TMP1 = "Client#(RegRequest#(%(asz)s, %(dsz)s), RegResponse#(%(dsz)s))"
        stmt = []
        name = self.parameters[0]['value']
        tname = self.parameters[2]['value'][1]
        dsz, asz= get_reg_array_size(name, json_dict)
        pdict = {'name': name, 'asz': asz, 'dsz': dsz}
        intf = ast.Interface(name, typeDefType = TMP1 % pdict)
        stmt.append(intf)
        return stmt

    def buildInterfaceDef(self):
        TMP1 = "interface %(name)s = toClient #(tx_info_%(name)s.e, rx_info_%(name)s.e);"
        stmt = []
        name = self.parameters[0]['value']
        tname = self.parameters[2]['value'][1]
        ptype = CamelCase(name)
        pdict = {"name": name, "tname": tname}
        stmt.append(ast.Template(TMP1, pdict))
        return stmt

class RemoveHeader(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class AddHeader(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class Drop(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class Nop(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class AddToField(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class SubtractFromField(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt

class CloneIngressPktToEgress(Primitive):
    def __init__(self, op, parameters):
        self.op = op
        self.parameters = parameters

    def build(self):
        stmt = []
        return stmt