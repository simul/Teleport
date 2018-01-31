import os
import shlex, subprocess
import sys

def execute():
	VSVER='14.0'
	wd=os.getcwd()
	os.environ['XboxOneExtensionSDKLatest']='C:\\Program Files (x86)\\Microsoft SDKs\\Durango.170604\\v8.0\\'
	os.environ['XboxOneXDKLatest']='C:\\Program Files (x86)\\Microsoft Durango XDK\\170604\\'
	os.environ['UE4_DIR']=os.path.normpath(os.getcwd())
	os.environ['VSDIR']=os.environ['ProgramFiles(x86)']+'/Microsoft Visual Studio '+VSVER;
	args=[os.environ['VSDIR']+'/Common7/IDE/devenv.exe','./RemotePlay.sln']

	pid=subprocess.Popen(args).pid

if __name__ == "__main__":
	execute()
