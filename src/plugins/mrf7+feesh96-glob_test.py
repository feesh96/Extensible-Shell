#!/usr/bin/python
#
# Block header comment
#
#
import sys, imp, atexit
sys.path.append("/home/courses/cs3214/software/pexpect-dpty/");
import pexpect, shellio, signal, time, os, re, proc_check

#Ensure the shell process is terminated
def force_shell_termination(shell_process):
	c.close(force=True)

#pulling in the regular expression and other definitions
definitions_scriptname = sys.argv[1]
plugin_dir = sys.argv[2]
def_module = imp.load_source('', definitions_scriptname)
logfile = None
if hasattr(def_module, 'logfile'):
    logfile = def_module.logfile

#spawn an instance of the shell
c = pexpect.spawn(def_module.shell + plugin_dir, drainpty=True, logfile=logfile)

atexit.register(force_shell_termination, shell_process=c)

# send the ls command with a single glob
c.sendline("ls *.c")

# Check output
from os import listdir
import re
shellio.parse_regular_expression(c, "(\S+\.c)")
# Get the output line
output = shellio.parse_regular_expression(c, "(.+)\r\n")
# Parse the output line for each file
output = re.findall("(\S+\.c)", output[0])

for file in listdir('.'):
	if file[-2:] == ".c":
		assert file in output

# Try a command with multiple globs
c.sendline("ls *.c *.h")

# Check output
from os import listdir
import re
shellio.parse_regular_expression(c, "(\S+\.h)")
# Get the output line
output = shellio.parse_regular_expression(c, "(.+)\r\n")
# Parse the output line for each file
output = re.findall("(\S+\.[ch])", output[0])
for file in listdir('.'):
	if file[-2:] == ".c" or file[-2:] == ".h":
		assert file in output
shellio.success()
