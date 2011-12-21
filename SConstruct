#
# Top level build file for the Advanced Operating System cs9242 project
# 
# Original Author:	Ben Leslie
# Author:		Godfrey van der Linden(gvdl)
#
# Date:			2006-06-23

#
# XXX: Student change this for the milestone you are building
milestone = 7

# First step is to include our real build tools tools/build.py includes the
# KengeEnvironment
try:
    execfile("tools/build.py")
except IOError:
    print 
    print "There was a problem finding the tools directory"
    print "This probably means you need to run:"
    print "  $ baz build-config packages"
    print
    import sys
    sys.exit()

# Instantiate a Kenge based environment for building and build a l4 kernel
env = KengeEnvironment()
l4kernel_env  = env.Copy("kernel")
l4kernel = l4kernel_env.Application("pistachio", aos=1)

# Build the sos application.  First create a build context for the "rootserver"
# this lets scons know which flags to use when building later libraries and
# application files.  You have to tell the environment all of the libraries
# that you may use with the AddLibrary function.  Each library lives in the
# libs directory wwith the function name provided.  For the driver project you
# will have to add the clock library too, I'd use libs/serial as an simple
# example.
rootserver_env = env.Copy("rootserver", LINKFLAGS=["-r"])
rootserver_env.AddLibrary("l4")
rootserver_env.AddLibrary("c", system="l4_rootserver")
rootserver_env.AddLibrary("elf")
rootserver_env.AddLibrary("ixp_osal")
rootserver_env.AddLibrary("ixp400_xscale_sw")
rootserver_env.AddLibrary("lwip")
rootserver_env.AddLibrary("nfs")
rootserver_env.AddLibrary("clock")
rootserver_env.AddLibrary("serial")
sos = rootserver_env.Application("sos")

timer_syscall_milestone = 3

# Use the app_env environment for building everything else that will run in the
# userland context.  Can't add libs/sos 'cause it doesn't exist yet.
app_env=env.Copy("userland", LINKFLAGS=["-Ttext=0x1000"])
app_env.AddLibrary("l4")
app_env.AddLibrary("c", system="sos")
app_env.AddLibrary("clock")
app_env.AddLibrary("elf")
app_env.AddLibrary("sos")
app_env1=env.Copy("userland-bootimage", LINKFLAGS=["-r"])
app_env1.AddLibrary("l4")
app_env1.AddLibrary("c", system="sos")
app_env1.AddLibrary("clock")
app_env1.AddLibrary("elf")
app_env1.AddLibrary("sos")

# Once you get to the later milestones you will be writing lots of little test
# tools that need to get loaded and run in your context.  Do that by calling
# the Application function a number times in the app_env environment.
sosh = app_env1.Application("sosh")
#hi = app_env.Application("hi")
#test1 = app_env.Application("test1")
#test2 = app_env.Application("test2")

# Bootimage takes a comma seperated list of Applications that are linked
# together into a single bootimg.bin binary.
bootimg = env.Bootimage(l4kernel, sos,sosh)

filesystem_apps=[]
#filesystem_apps.append(app_env.Application("sosh"))
filesystem_apps.append(app_env.Application("hi"))
filesystem_apps.append(app_env.Application("test1"))
filesystem_apps.append(app_env.Application("test2"))


Default(bootimg) # Default build target is the bootimage.
Default(filesystem_apps)
Default(app_env.Install ("/mnt/hgfs/nfs",filesystem_apps))
# vim:ft=python:
