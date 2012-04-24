import xmlrpclib
import os

import networkx as nx
from networkx import Graph

import matplotlib.colors as c

class MyceliaServer:
    # This is designed to be only a partial class.
    # You must use multiple-inheritance. 

    def open_file(self, path):
        # Relative paths are interpreted relative to the CWD of 'mycelia',
        # not relative to the CWD of the Python interpreter. Eventually, we can
        # implement a file opener in Python.
        self.server.open_file(os.path.abspath(path))

    def __init__(self, server='http://localhost:9876', label='label'):
        self.server = xmlrpclib.Server(server)
        self.label = label
        self.myid = 'mycelia_id'

        self.node_types = ['shape', 'image']
        self.texture_modes = ['align', 'rotate']
        self.layout_types = {'static':0, 'dynamic':1}
                
        self.graph_attrs = [
            'texture_mode',
        ]
            
        self.edge_attrs = [
            'label',
            'weight'
        ]

        self.node_attrs = [
            self.label,
            'size',
            'color',
            'type',
            'image',
        ]
            
    def _parse_node_attrs(self, myid, attrs):
        label = attrs.get(self.label, None)
        if label is not None:
            self.server.set_node_label(myid, str(label))
        
        if 'color' in attrs:
            r,g,b,a = c.colorConverter.to_rgba(attrs['color'])
            self.server.set_node_color(myid, r, g, b, a)
            
        if 'size' in attrs:
            self.server.set_node_size(myid, float(attrs['size']))
            
        if 'image' in attrs:
            path = attrs['image']
            
            if path:
                path = os.path.abspath(path)
                self.server.set_node_type(myid, 'image')
                self.server.set_node_image_path(myid, path)
                
    def _parse_edge_attrs(self, myid, attrs):
        label = attrs.get(self.label, None)
        if label is not None:
            self.server.set_edge_label(myid, label)
                
        weight = attrs.get('weight', None)
        if weight is not None:
            self.server.set_edge_weight(myid, float(weight))
    
    def center(self):
        self.server.center()
        
    def clear_edges(self):
        for u,v in self.edges():
            self.remove_edge(u,v)
            
    def draw(self):
        self.server.draw()
        
    def layout(self):
        self.server.layout()

    def start_layout(self):
        self.server.start_layout()
        
    def stop_layout(self):
        self.server.stop_layout()
        
    def set_layout_type(self, layout):
        if layout not in self.layout_types:
            raise Exception("Layout should be 'static' or 'dynamic'.")
        else:
            self.server.set_layout_type(self.layout_types[layout])
            
    def add_node_at(self, n, pos, attr_dict=None, **attr):
        nx.Graph.add_node(self,n,attr_dict=attr_dict,**attr)
        myid = self.server.add_node_at(float(pos[0]),
                                       float(pos[1]),
                                       float(pos[2]))        
        self.node[n][self.myid] = myid
                
    def set_texture_node_mode(self, mode):
        if mode not in self.texture_modes:
            raise Exception("Mode should be 'align' or 'rotate'.")
        else:
            self.server.set_texture_node_mode(mode)
    
    


class MyceliaGraph(nx.Graph, MyceliaServer):

    def __init__(self, server='http://localhost:9876', label='label'):
        Graph.__init__(self)
        MyceliaServer.__init__(self, server, label)
        self.clear()
        
    def clear(self):
        Graph.clear(self)
        self.server.clear()        
        
    def add_node(self, n, attr_dict=None, **attr):
        if n in self:
            existing = True
        else:
            existing = False
            
        nx.Graph.add_node(self,n,attr_dict=attr_dict,**attr)        
        if not existing:
            myid = self.server.add_node()
            self.node[n][self.myid] = myid
        else:
            myid = self.node[n][self.myid]

        self._parse_node_attrs(myid, self.node[n])
        return myid

    def add_nodes_from(self, nodes, **attr):
        for n in nodes:
            self.add_node(n, **attr)

    def remove_node(self,n):
        myid = self.node[n].get(self.myid, None)
        if myid is not None:
            Graph.remove_node(self,n)
            self.server.delete_node(myid)

    def remove_nodes_from(self, nodes):
        for n in nodes:
            self.remove_node(n)

    def add_edge(self, u, v, attr_dict=None, **attr):
        if (u,v) in self.edge:
            existing = True
        else:
            existing = False
        myid_u = self.add_node(u)
        myid_v = self.add_node(v)
        Graph.add_edge(self,u,v,attr_dict=attr_dict,**attr)
        
        if not existing:
            myid = self.server.add_edge(myid_u, myid_v)
            # automatically bidirectional 
            self.edge[u][v][self.myid] = myid
        else:
            myid = self.edge[u][v][self.myid]

        self._parse_edge_attrs(myid, self.edge[u][v])

        
    def add_edges_from(self, ebunch, attr_dict=None, **attr):  
        for e in ebunch:
            u,v=e[0:2]
            self.add_edge(u,v,attr_dict=attr_dict,**attr)

    def remove_edge(self, u, v): 
        myid_u = self.node[u][self.myid]
        myid_v = self.node[v][self.myid]
        if myid_u is not None and myid_v is not None:
            Graph.remove_edge(self,u,v)
            self.server.delete_edge(myid_u, myid_v)    

    def remove_edges_from(self, ebunch): 
        for e in ebunch:
            u,v=e[0:2]
            self.remove_edge(u,v)
            

