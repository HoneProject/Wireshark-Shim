; Inno Setup script to build the installer for the Hone (Host-Network)
; Packet-Process Correlator Wireshark Live-Capture Shim
;
; Copyright (c) 2014 Battelle Memorial Institute
; Licensed under a modification of the 3-clause BSD license
; See License.txt for the full text of the license and additional disclaimers

#include "version.ini"
#define MyAppId        "{018D6736-28F0-473B-AB91-6BF19F44E88F}"
#define MyAppName      "Hone Wireshark Shim"
#define MyAppPublisher "Pacific Northwest National Laboratory"
#define MyAppURL       "http://www.pnl.gov/"

[Setup]
AppComments=Hone (Host-Network) Packet-Process Correlator Wireshark Live-Capture Shim
AppId={{#MyAppId}
AppName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
AppVersion={#MyAppVersion}
ArchitecturesAllowed=x86 x64
ArchitecturesInstallIn64BitMode=x64
Compression=lzma2
DefaultDirName={pf}\PNNL\{#MyAppName}
DefaultGroupName=PNNL\{#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=License.rtf
MinVersion=6.1
OutputBaseFilename=Hone-WS
SetupIconFile=hone.ico
SolidCompression=yes
UninstallDisplayIcon={app}\hone.ico
VersionInfoVersion={#MyAppVersion}
WizardImageFile=hone_164x314.bmp
WizardSmallImageFile=hone_55x58.bmp

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "License.txt";  DestDir: "{app}"
Source: "Readme.html";  DestDir: "{app}"
Source: "add.ico";      DestDir: "{app}"
Source: "hone.ico";     DestDir: "{app}"
Source: "rem.ico";      DestDir: "{app}"
Source: "trash.ico";    DestDir: "{app}"

Source: "hone-dumpcap.exe"; DestDir: "{app}"
Source: "hook.exe";         DestDir: "{app}"
Source: "icudt52.dll";      DestDir: "{app}"
Source: "icuin52.dll";      DestDir: "{app}"
Source: "icuuc52.dll";      DestDir: "{app}"
Source: "msvcp110.dll";     DestDir: "{app}"
Source: "msvcr110.dll";     DestDir: "{app}"
Source: "Qt5Core.dll";      DestDir: "{app}"

[Icons]
Name: "{group}\Readme";               WorkingDir: "{app}"; Comment: "View Hone Wireshark Live-Capture Shim Documentation";       Filename: "{app}\Readme.html"
Name: "{group}\Set Wireshark Shim";   WorkingDir: "{app}"; Comment: "Install Hone Wireshark Live-Capture Shim into Wireshark";   Filename: "{app}\hook.exe"; Parameters: "install -p";   IconFilename: "{app}\add.ico"
Name: "{group}\Unset Wireshark Shim"; WorkingDir: "{app}"; Comment: "Uninstall Hone Wireshark Live-Capture Shim from Wireshark"; Filename: "{app}\hook.exe"; Parameters: "uninstall -p"; IconFilename: "{app}\rem.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}";                                                                              Filename: "{uninstallexe}";                             IconFilename: "{app}\trash.ico"

[Run]
Filename: "{app}\hook.exe"; Flags: runhidden; Parameters: "install"

[UninstallRun]
Filename: "{app}\hook.exe"; Flags: runhidden; Parameters: "uninstall"

[Code]
// Adapted from http://www.lextm.com/2007/08/inno-setup-script-sample-for-version-comparison-2/
function GetNumber(var temp: String): Integer;
var
	part: String;
	pos1: Integer;
begin
	if Length(temp) = 0 then
	begin
		Result := -1;
		Exit;
	end;
	pos1 := Pos('.', temp);
	if (pos1 = 0) then
	begin
		Result := StrToInt(temp);
		temp := '';
	end
	else
	begin
		part := Copy(temp, 1, pos1 - 1);
		temp := Copy(temp, pos1 + 1, Length(temp));
		Result := StrToInt(part);
	end;
end;

function CompareInner(var temp1, temp2: String): Integer;
var
	num1, num2: Integer;
begin
	num1 := GetNumber(temp1);
	num2 := GetNumber(temp2);
	if (num1 = -1) or (num2 = -1) then
	begin
		Result := 0;
		Exit;
	end;
	if (num1 > num2) then
	begin
		Result := 1;
	end
	else if (num1 < num2) then
	begin
		Result := -1;
	end
	else
	begin
		Result := CompareInner(temp1, temp2);
	end;
end;

function CompareVersion(str1, str2: String): Integer;
var
	temp1, temp2: String;
begin
	temp1 := str1;
	temp2 := str2;
	Result := CompareInner(temp1, temp2);
end;

// Check if newer version is already installed
function InitializeSetup(): Boolean;
var
	oldVersion:     String;
	versionCompare: Integer;
begin
	Result := True;
	if RegKeyExists(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1') then
	begin
		RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1', 'DisplayVersion', oldVersion);
		versionCompare := CompareVersion(oldVersion, '{#MyAppVersion}');
		if (versionCompare < 0) then
		begin
			if (WizardSilent = False) and (MsgBox('{#MyAppName}' + oldVersion + ' is installed. Do you want to upgrade to {#MyAppVersion}?', mbConfirmation, MB_YESNO) = IDNO) then
			begin
				Result := False;
			end;
		end
		else if (versionCompare > 0) then
		begin
			if (WizardSilent = False) then
			begin
				MsgBox('{#MyAppName}' + oldVersion + ' is newer than {#MyAppVersion}. The installer will now exit.', mbInformation, MB_OK);
			end;
			Result := False;
		end
		else
		begin
			if (WizardSilent = False) and (MsgBox('{#MyAppName}' + oldVersion + ' is already installed. Do you want to reinstall?', mbConfirmation, MB_YESNO) = IDNO) then
			begin
				Result := False;
			end;
		end;
	end;
end;
