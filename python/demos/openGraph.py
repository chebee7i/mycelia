import os
import time
import xmlrpclib

from nxwrapper import MyceliaGraph

g = MyceliaGraph()
g.open_file('/home/ellisocj/dev/git/mycelia/data/actor100.xml')

pwd = '/home/ellisocj/business/000/archive/metacore.ucdavis.edu/techno1/compounds/'
x = os.listdir(pwd)

time.sleep(3)
for i,img in enumerate([x[0]]*100):
    if img.endswith('png'):
        z = pwd+img
        g.server.set_node_type(i, 'image')
        g.server.set_node_image_path(i, z)


