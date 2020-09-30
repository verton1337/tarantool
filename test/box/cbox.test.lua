build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..package.cpath

cbox = require('cbox')
fio = require('fio')

ext = (jit.os == "OSX" and "dylib" or "so")

cfunc_path = fio.pathjoin(build_path, "test/box/cfunc.") .. ext
cfunc1_path = fio.pathjoin(build_path, "test/box/cfunc1.") .. ext
cfunc2_path = fio.pathjoin(build_path, "test/box/cfunc2.") .. ext

_ = pcall(fio.unlink(cfunc_path))
fio.symlink(cfunc1_path, cfunc_path)

--
-- They all are sitting in cfunc.so
cfunc_nop = cbox.func.load('cfunc.cfunc_nop')
cfunc_fetch_evens = cbox.func.load('cfunc.cfunc_fetch_evens')
cfunc_multireturn = cbox.func.load('cfunc.cfunc_multireturn')
cfunc_args = cbox.func.load('cfunc.cfunc_args')
cfunc_sum = cbox.func.load('cfunc.cfunc_sum')

--
-- Make sure they all are callable
cfunc_nop()
cfunc_fetch_evens()
cfunc_multireturn()
cfunc_args()

--
-- Clean old function references and reload a new one.
_ = pcall(fio.unlink(cfunc_path))
fio.symlink(cfunc2_path, cfunc_path)

cbox.module.reload('cfunc')

cfunc_nop()
cfunc_multireturn()
cfunc_fetch_evens({2,4,6})
cfunc_fetch_evens({1,2,3})  -- error
cfunc_args(1, "hello")
cfunc_sum(1) -- error
cfunc_sum(1,2)

--
-- Clean it up
cbox.func.unload('cfunc.cfunc_nop')
cbox.func.unload('cfunc.cfunc_fetch_evens')
cbox.func.unload('cfunc.cfunc_multireturn')
cbox.func.unload('cfunc.cfunc_args')

--
-- Cleanup the generated symlink
_ = pcall(fio.unlink(cfunc_path))
