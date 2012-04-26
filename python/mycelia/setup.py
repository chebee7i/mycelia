#!/usr/bin/env python
# -*- coding: utf-8 -*-

import os
import sys
import warnings
from glob import glob

import distutils
from distutils.core import setup
from distutils.command import install_data

class my_install_data(install_data.install_data):
    # A custom install_data command, which will install it's files
    # into the standard directories (normally lib/site-packages).
    def finalize_options(self):
        if self.install_dir is None:
            installobj = self.distribution.get_command_obj('install')
            self.install_dir = installobj.install_lib
        print 'Installing data files to %s' % self.install_dir
        install_data.install_data.finalize_options(self)

def main():

    cmdclass = {'install_data': my_install_data}

    data_files = ()

    requires = [
                'numpy(>1.2)',
                'matplotlib(>1.2.0)',
                'networkx(>1.4)',
               ]

    packages = ['mycelia',
               ]

    setup(
          name             = "mycelia",
          provides         = ['mycelia'],
          version          = "0.1",
          packages         = packages,
          cmdclass         = cmdclass,
          data_files       = data_files,
     )


if __name__ == '__main__':
    if sys.argv[-1] == 'setup.py':
        print "To install, run 'python setup.py install'\n"

    main()
