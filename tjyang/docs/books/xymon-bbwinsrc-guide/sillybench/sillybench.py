#!/usr/bin/python
#
# Silly benchmarking program, to give a vague idea of how fast a few
# tools are on a handful of common operations.
#
# Use a fairly big and real source tarball to test with: Firefox
# 2.0.0.3 (37622 files, 5374 directories, 343MB unpacked onto
# 4KB-blocksize ext3).

import csv
import os
import shutil
import sys
import tempfile
import time
import urllib2

url = 'ftp://ftp.mozilla.org/pub/mozilla.org/firefox/releases/2.0.0.3/source/firefox-2.0.0.3-source.tar.bz2'

class CommandFailure(Exception):
    pass

class rcs(object):
    def __init__(self):
        self.logfp = open(self.__class__.__name__ + '.csv', 'w')
        self.csv = csv.writer(self.logfp)

    def download(self):
        name = url[url.rfind('/')+1:]
        path = os.path.join(os.environ['HOME'], name)
        if not os.path.isfile(path):
            ofp = open(path + '.part', 'wb')
            try:
                ifp = urllib2.urlopen(url)
                nbytes = ifp.info()['content-length']
                sys.stdout.write('%s: %s bytes ' % (name, nbytes))
                sys.stdout.flush()
                while True:
                    data = ifp.read(131072)
                    if not data: break
                    sys.stdout.write('.')
                    sys.stdout.flush()
                    ofp.write(data)
                del ofp
                os.rename(path + '.part', path)
            except:
                if os.path.exists(path + '.part'):
                    os.unlink(path + '.part')
                if os.path.exists(path):
                    os.unlink(path)
                raise
        return path

    def run(self, args, mustsucceed=True):
        ret = os.spawnvp(os.P_WAIT, args[0], args)
        if ret < 0:
            msg = 'killed by signal %d' % (-ret)
        if ret > 0:
            msg = 'exited with status %d' % (ret)
        if ret:
            if mustsucceed:
                raise CommandFailure('%s: %s' % (msg, ' '.join(args)))
            print >> sys.stderr, 'WARNING: %s: %s' % (msg, ' '.join(args))

    def time(self, *args, **kwargs):
        start = time.time()
        self.run(*args, **kwargs)
        end = time.time()
        return end - start
        
    def logtime(self, name, elapsed, rest=[]):
        self.log('time:' + name, '%.3f' % elapsed, rest)

    def log(self, name, value, rest=[]):
        item = (name, value, repr(rest))
        print ' '.join(item)
        self.csv.writerow(item)
        self.logfp.flush()

    def unpack(self):
        tarball = self.download()
        t = self.time(['tar', '-C', self.wdir, '-jxf', tarball])
        self.logtime('internal:untar', t)
        for name in os.listdir(os.path.join(self.wdir, 'mozilla')):
            os.rename(os.path.join(self.wdir, 'mozilla', name),
                      os.path.join(self.wdir, name))

    def cleanup(self):
        pass

    def add(self, paths):
        pass

    def commit(self, msg, paths):
        pass

    def status(self, path):
        pass

    def remove(self, path):
        pass


class subversion(rcs):
    def __init__(self, root):
        rcs.__init__(self)
        self.repo = os.path.join(root, 'repo')
        self.wdir = os.path.join(root, 'wc')
        create = self.time(['svnadmin', 'create', '--fs-type=fsfs', self.repo])
        self.logtime('svn:create', create)
        co = self.time(['svn', 'co', 'file://' + self.repo, self.wdir])
        self.logtime('svn:co', co)
        self.logtime('init', create + co)
        os.chdir(self.wdir)

    def dropmeta(self, names):
        return [n for n in names if os.path.basename(n) != '.svn']

    def add(self, paths):
        t = self.time(['svn', 'add', '-q'] + paths)
        self.logtime('add %r' % paths, t)

    def commit(self, msg, paths=[]):
        if paths:
            t = self.time(['svn', 'ci', '-q', '-m', msg] + paths)
        else:
            t = self.time(['svn', 'ci', '-q', '-m', msg])
        self.logtime('commit %r' % paths, t)


class mercurial(rcs):
    def __init__(self, root):
        rcs.__init__(self)
        self.repo = os.path.join(root, 'repo')
        self.wdir = self.repo
        init = self.time(['hg', 'init', self.repo])
        self.logtime('init', init)
        os.chdir(self.wdir)

    def dropmeta(self, names):
        return [n for n in names if os.path.basename(n) != '.hg']

    def add(self, paths):
        t = self.time(['hg', 'add', '-q'] + paths)
        self.logtime('add %r' % paths, t)

    def commit(self, msg, paths=[]):
        if paths:
            t = self.time(['hg', 'ci', '-q', '-m', msg] + paths)
        else:
            t = self.time(['hg', 'ci', '-q', '-m', msg])
        self.logtime('commit %r' % paths, t)

def benchmark(cls):
    oldcwd = os.getcwd()
    root = tempfile.mkdtemp(prefix='sillybench.')
    try:
        print 'root', root
        inst = cls(root)
        inst.unpack()
        names = inst.dropmeta(os.listdir('.'))
        dirs = [n for n in names if os.path.isdir(n)]
        nondirs = [n for n in names if not os.path.isdir(n)]
        dirs.sort(key=hash)
        names.sort(key=hash)
        for d in dirs[:len(dirs)/2]:
            inst.add([d])
            inst.commit('Add %r' % d, [d])
        inst.add(dirs[len(dirs)/2:] + names)
        inst.commit('Add remaining dirs and files')
    finally:
        print >> sys.stderr, '[cleaning up...]'
        shutil.rmtree(root)
        os.chdir(oldcwd)

benchmark(mercurial)
#benchmark(subversion)
