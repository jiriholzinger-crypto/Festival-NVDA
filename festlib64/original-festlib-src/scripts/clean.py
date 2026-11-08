#This script is used to remove the unnecessary files from the festival\lib directory.
import sys
from os import remove
from os.path import join,isfile,walk

def main(d):
	walk(d,check,["Makefile","VCMakefile","VCLocalRules"])

def check(l,d,names):
	for name in names:
		f=join(d,name)
		if (name in l) and isfile(f):
			remove(f)

if __name__=="__main__":
	main(sys.argv[1])