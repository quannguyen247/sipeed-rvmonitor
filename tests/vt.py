# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
"""Stdlib grid replay for the exact ASCII/CSI subset emitted by rvmonitor.

This checks visible overwrite/erase behavior, not fonts or full Konsole emulation.
Unknown control sequences fail instead of being silently treated as text.
"""
import re

class Screen:
    def __init__(self, columns=80, rows=24):
        self.columns,self.rows=columns,rows
        self.cells=[[' ']*columns for _ in range(rows)]
        self.x=self.y=0;self.pending=b'';self.saved=None;self.frames=[]
    def lines(self):return [''.join(row).rstrip() for row in self.cells]
    def clear(self):self.cells=[[' ']*self.columns for _ in range(self.rows)]
    def newline(self):
        self.y+=1
        if self.y==self.rows:
            self.cells.pop(0);self.cells.append([' ']*self.columns);self.y-=1
    def feed(self,data):
        self.pending+=data;i=0
        while i<len(self.pending):
            byte=self.pending[i]
            if byte==27:
                match=re.match(rb'\x1b\[([0-?]*)([ -/]*)([@-~])',self.pending[i:])
                if not match:
                    if len(self.pending)-i<32:break
                    raise ValueError('Unsupported/incomplete escape')
                params,_,final=match.groups();i+=match.end()
                if final==b'H':
                    parts=params.decode().split(';') if params else []
                    self.y=min(self.rows-1,max(0,(int(parts[0] or '1') if parts else 1)-1))
                    self.x=min(self.columns-1,max(0,(int(parts[1] or '1') if len(parts)>1 else 1)-1))
                elif final==b'J':
                    mode=int(params or b'0')
                    if mode==2:self.clear()
                    elif mode==0:
                        self.cells[self.y][self.x:]=[' ']*(self.columns-self.x)
                        for row in range(self.y+1,self.rows):self.cells[row]=[' ']*self.columns
                        self.frames.append(self.lines())
                    else:raise ValueError(('Unsupported ED',mode))
                elif final==b'm':pass
                elif final in [b'h',b'l'] and params==b'?25':pass
                elif final in [b'h',b'l'] and params==b'?1049':
                    if final==b'h':
                        self.saved=([row[:] for row in self.cells],self.x,self.y);self.clear();self.x=self.y=0
                    elif self.saved:self.cells,self.x,self.y=self.saved;self.saved=None
                else:raise ValueError(('Unsupported CSI',params,final))
                continue
            i+=1
            if byte==13:self.x=0
            elif byte==10:self.newline()
            elif byte==8:self.x=max(0,self.x-1)
            elif byte==9:self.x=min(self.columns-1,((self.x//8)+1)*8)
            elif 32<=byte<127:
                if self.x>=self.columns:self.x=0;self.newline()
                self.cells[self.y][self.x]=chr(byte);self.x+=1
            else:raise ValueError(('Unsupported non-ASCII byte',byte))
        self.pending=self.pending[i:]
