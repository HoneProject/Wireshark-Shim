#------------------------------------------------------------------------------
# Script to build the Hone (Host-Network) Packet-Process Correlator
# Wireshark Live-Capture Shim
#
# Copyright (c) 2014 Battelle Memorial Institute
# Licensed under a modification of the 3-clause BSD license
# See License.txt for the full text of the license and additional disclaimers
#
# Authors
#   Richard L. Griswold <richard.griswold@pnnl.gov>
#------------------------------------------------------------------------------

import argparse
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys
import traceback

#------------------------------------------------------------------------------
def build_installer(args):
	if not os.path.exists(args.build_root):
		print 'Error: Build root {0} does not exist\n' \
				'Check version or try rebuilding'.format(args.build_root)
		exit(1)

	cwd = os.getcwd()
	files_dir = '{0}/files'.format(args.build_root)
	installer_files = []

	print ' * Building Hone Wireshark Shim installer {0}'.format(args.version)

	# Create destination directories
	dst_dir = '{0}/temp/installer'.format(args.build_root)
	if os.path.exists(dst_dir):
		clean_dir(dst_dir)
	else:
		os.makedirs(dst_dir)

	# Copy files needed by installer
	print ' * Copying files needed by installer...'
	copy_all('{0}/build_files'.format(args.build_root), dst_dir)
	copy_all('{0}/installer'.format(args.script_dir), dst_dir)
	copy_file('{0}/License.txt'.format(args.script_dir), dst_dir)
	copy_file('{0}/Readme.html'.format(args.script_dir), dst_dir)

	# Create installer version file
	print ' * Creating version file...'
	with open('{0}/version.ini'.format(dst_dir), 'w') as f:
		f.write('#define MyAppVersion "{0}"\n'.format(args.version))

	os.chdir(dst_dir)

	# Build the installer
	try:
		cmdline = [args.iscc, 'hone-ws.iss']
		retcode = subprocess.call(cmdline)
	except Exception:
		traceback.print_exc(1)
		exit(1)
	if retcode != 0:
		print 'Error: Inno Setup compilation failed with error code {0}'.format(retcode)
		exit(1)

	# Copy the installer to the output directory
	installer_dir = '{0}/installers'.format(args.build_root)
	if not os.path.exists(installer_dir):
		os.makedirs(installer_dir)

	installer_src = '{0}/Output/Hone-WS.exe'.format(dst_dir)
	installer_dst = '{0}/Hone-WS-{1}-win7.exe'.format(installer_dir, args.version)
	shutil.copy(installer_src, installer_dst)

	installer_files += [installer_dst,]
	os.chdir(cwd)
	return installer_files

#------------------------------------------------------------------------------
def build_qt(args, name, src_dir, pro_file):
	print ' * Building {0} {1}'.format(name, args.version)

	cwd = os.getcwd()
	build_dir = '{0}/temp/{1}'.format(args.build_root, src_dir)
	if os.path.exists(build_dir):
		clean_dir(build_dir)
	else:
		os.makedirs(build_dir)
	os.chdir(build_dir)

	try:
		cmdline = ['qmake', '{0}/{1}/{2}'.format(args.script_dir, src_dir, pro_file)]
		retcode = subprocess.call(cmdline)
	except Exception:
		traceback.print_exc(1)
		exit(1)
	if retcode != 0:
		print 'Error: qmake failed with error code {0}'.format(retcode)
		exit(1)

	try:
		cmdline = ['nmake', 'release']
		retcode = subprocess.call(cmdline)
	except Exception:
		traceback.print_exc(1)
		exit(1)
	if retcode != 0:
		print 'Error: nmake failed with error code {0}'.format(retcode)
		exit(1)

	print ' * Copying build files...'
	dst_dir = '{0}/build_files'.format(args.build_root)
	if not os.path.exists(dst_dir):
		os.makedirs(dst_dir)
	copy_exe('{0}/release'.format(build_dir), dst_dir)

	dst_dir = '{0}/debug_symbols'.format(args.build_root)
	if not os.path.exists(dst_dir):
		os.makedirs(dst_dir)
	copy_pdb('{0}/release'.format(build_dir), dst_dir)

	os.chdir(cwd)

#------------------------------------------------------------------------------
def clean_dir(path):
	for root, dirs, files in os.walk(path):
		for f in files:
			file = '{0}/{1}'.format(root, f)
			# print '   Removing {0}'.format(file)
			os.unlink(file)
		for d in dirs:
			dir = '{0}/{1}'.format(root, d)
			print '   Removing {0}'.format(dir)
			shutil.rmtree(dir)

#------------------------------------------------------------------------------
def copy_all(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*'.format(src_dir)):
		fname = fname.replace('\\', '/')
		if os.path.isfile(fname):
			print '   {0}'.format(fname)
			shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_dlls(args):
	print ' * Copying DLLs...'
	dst_dir = '{0}/build_files'.format(args.build_root)
	if not os.path.exists(dst_dir):
		os.makedirs(dst_dir)

	for qt_dll in ['Qt5Core.dll', 'icuin52.dll', 'icuuc52.dll', 'icudt52.dll']:
		fname = '{0}/{1}'.format(args.qt_dir, qt_dll);
		print '   {0}'.format(fname)
		shutil.copy(fname, dst_dir)

	for vc_dll in ['msvcr110.dll', 'msvcp110.dll']:
		fname = '{0}redist/x86/Microsoft.VC110.CRT/{1}'.format(args.vc_install_dir,
				vc_dll);
		print '   {0}'.format(fname)
		shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_exe(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*.exe'.format(src_dir)):
		fname = fname.replace('\\', '/')
		print '   {0}'.format(fname)
		shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_file(src_file, dst_dir):
	fname = src_file.replace('\\', '/')
	print '   {0}'.format(fname)
	shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_pdb(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*.pdb'.format(src_dir)):
		if fname.find('vc90.pdb', -8) == -1:
			fname = fname.replace('\\', '/')
			print '   {0}'.format(fname)
			shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def create_version_file(path, version):
	'''Create version file'''
	time = datetime.datetime.now()
	year = time.year % 100;
	timestamp = '{0}-{1:02}-{2:02} {3:02}:{4:02}:{5:02}'.format(time.year,
			time.month, time.day, time.hour, time.minute, time.second)

	version_list = version.replace('.', ',')
	with open('{0}/version.h'.format(path), 'w') as f:
		f.write('''\
// -=-=-=-=-=- DO NOT EDIT THIS FILE! -=-=-=-=-=-
// Hone Wireshark Shim version number
// Automatically generated by build.py on {0}
#define HONE_WS_PRODUCTVERSION      {1}
#define HONE_WS_PRODUCTVERSION_STR  "{2}"
'''.format(timestamp, version_list, version))

#------------------------------------------------------------------------------
def parse_args(argv):
	help_epilog='''\
The script builds the Hone (Host-Network) Packet-Process Correlator
Wireshark Live-Capture Shim.  It creates the build directories as follows:
+-----------------+--------------------------------------+
| hone_ws_n.n.n   | Build root                           |
|   build_files   |   Files created by the build process |
|   debug_symbols |   PDB files for executables          |
|   installers    |   Installers                         |
|   temp          |   Temporary files                    |
|     build       |     Temporary build files            |
|     installer   |     Temporary installer files        |
+-----------------+--------------------------------------+
You can delete the temp directory after the script finishes.
'''

	parser = argparse.ArgumentParser(add_help=False, epilog=help_epilog,
			formatter_class=argparse.RawTextHelpFormatter)

	group = parser.add_argument_group()
	group.add_argument('-h', '--help', action='help',
			help='show this help message and exit')
	group.add_argument('-s', dest='stage', default='a',
			help='stage to run (a=all [default], b=build, i=create installer)')
	group.add_argument('-o', dest='output', metavar='DIR',
			help='output directory (default: script directory)')
	group.add_argument('-v', dest='version',
			help='set build version as "major.minor" or "major.minor.build"')

	args = parser.parse_args(argv[1:])
	error = []

	# Validate arguments
	if not re.match('[abi]*$', args.stage):
		error.append('Invalid stage specification "{0}"'.format(args.stage))
	if 'a' in args.stage:
		args.stage = ['b', 'i']
	else:
		args.stage = sorted(set(list(args.stage)))

	if not args.version:
		error.append('Version required')
	elif not re.match('[0-9]+\.[0-9]+(\.[0-9]+)?$', args.version):
		error.append('Invalid version "{0}": version format must be "major.minor" '
				'or "major.minor.build", where each version element is a number'
				.format(args.version))
	else:
		version = args.version.split('.')
		if int(version[0]) > 255:
			error.append('Invalid version "{0}": major version number '
					'must be less than 256'.format(args.version))
		if int(version[1]) > 255:
			error.append('Invalid version "{0}": minor version number '
					'must be less than 256'.format(args.version))
		if (len(version) > 2) and (int(version[2]) > 65535):
			error.append('Invalid version "{0}": build version number '
					'must be less than 65536'.format(args.version))

	if 'i' in args.stage:
		for pf_env in ['ProgramFiles', 'ProgramFiles(x86)']:
			pf = os.environ.get(pf_env)
			if pf:
				iscc = '{0}/Inno Setup 5/iscc.exe'.format(pf)
				if os.path.exists(iscc):
					args.iscc = iscc
					break
		if not hasattr(args, 'iscc'):
			error.append('Inno Setup 5 compiler not found - Ensure Inno Setup 5 '
					'is installed to the standard Program Files location.')

	# Find Qt installation directory
	for path in os.environ.get('PATH').split(';'):
		path = path.replace('\\', '/')
		file = '{0}/Qt5Core.dll'.format(path)
		if (os.path.exists(file)):
			args.qt_dir = path
			break
	if not hasattr(args, 'qt_dir'):
		error.append('Qt install directory not found - Ensure you are running '
				'in a Qt command window with Administrator privileges')

	# Make sure the VS2012 environment is set up
	args.vc_install_dir = os.environ.get('VCINSTALLDIR')
	if args.vc_install_dir is None:
		for pf_dir in ['Program Files', 'Program Files (x86)']:
			vs_path = 'C:\\{0}\\Microsoft Visual Studio 11.0\\Common7\\Tools\\VsDevCmd.bat'.format(pf_dir)
			if (os.path.exists(vs_path)):
				break
			else:
				vs_path = None
		if not vs_path:
			error.append('Visual Studio 2012 not found')
		else:
			error.append('Visual Studio 2012 environment is not configured - In a '
					'Qt command window with Administrator privileges, run:\n   "{0}"'
					.format(vs_path));
	else:
		args.vc_install_dir = args.vc_install_dir.replace('\\', '/')

	if error:
		error.sort()
		parser.error('\n * {0}'.format('\n * '.join(error)))

	# Set script path, build root, and output
	args.script_dir = os.path.abspath(os.path.dirname(argv[0])).replace('\\', '/')

	if args.output:
		args.output = os.path.abspath(args.output).replace('\\', '/')
	else:
		args.output = args.script_dir

	args.build_root = '{0}/hone_ws_{1}'.format(args.output, args.version)

	return args

#------------------------------------------------------------------------------
def main(argv):
	args = parse_args(argv)

	if 'b' in args.stage:
		if not os.path.exists(args.build_root):
			os.makedirs(args.build_root)
		create_version_file(args.script_dir, args.version)
		copy_dlls(args)
		build_qt(args, 'Hone Wireshark Shim', 'shim', 'hone_dumpcap.pro')
		build_qt(args, 'Hone Wireshark Hook', 'hook', 'hook.pro')

	installer_files = []
	if 'i' in args.stage:
		installer_files = build_installer(args)

	print ' * Build completed successfully!'
	if installer_files:
		for installer_file in installer_files:
			print '   {0}'.format(installer_file)

#------------------------------------------------------------------------------
if __name__ == '__main__':
	try:
		main(sys.argv)
	except KeyboardInterrupt:
		pass
