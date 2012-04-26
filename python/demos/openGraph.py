import os
import glob
import time
import itertools

from mycelia import Graph

def take(iterable, n):
    "Return first n items of the iterable as a list"
    return list(itertools.islice(iterable, n))


g = Graph()
g.open_file('/home/ellisocj/dev/git/mycelia/data/actor100.xml')

image_dir = '/usr/share/pixmaps/'
glb = '*.png'
filenames = glob.glob(image_dir + glb)[:68]
images = take(itertools.cycle(filenames), 100)

time.sleep(1)
        
for i,img in enumerate(images):
    g.server.set_node_type(i, 'image')
    g.server.set_node_image_path(i, img)


