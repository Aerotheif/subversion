(* is_main.pas: Pascal Scripts routines for Inno Setup Windows installer.
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 *)

// ****************************************************************************
// Global variables
var
    // Required dlls
    g_bMsVcpNotFound: Boolean;                // Visual C++ 6.0 Runtimes
    g_bShFolderNotFound: Boolean;             // shfolder.dll

    // Apache
    g_bHandleApache: Boolean;
    g_sApachePath: String;
    g_sApachePathBin: String;
    g_sApachePathConf: String;
    g_sApachePathModules: String;

const
    // Visual C++ 6.0 Runtime file related
    FILE_MSVCPDLL = 'msvcp60.dll';
    URL_VCREDIST = 'http://support.microsoft.com/support/kb/articles/Q259/4/03.ASP';

    // shfolder.dll related
    FILE_SHFOLDERDLL = 'shfolder.dll';
    URL_SHFREDIST = 'http://download.microsoft.com/download/platformsdk/Redist/5.50.4027.300/W9XNT4/EN-US/shfinst.EXE';

    // Apache
    REG_KEY_APACHE_SERVICE = 'SYSTEM\CurrentControlSet\Services\Apache2';
    APACHE_VER_MIN = '{#= apache_ver_min}';

    // Status codes for modules in httpd.conf
    STATUS_NONE = 0;
    STATUS_DISABLED = 1;
    STATUS_ENABLED = 2;

// ***************************************************************************
// UninsHs stuff
function ComponentList(Default: string):string;
begin
    Result := WizardSelectedComponents(False);
end;

function SkipCurPage(CurPage: Integer): Boolean;
begin
    if Pos('/SP-', UpperCase(GetCmdTail)) > 0 then
        case CurPage of
          wpWelcome, wpLicense, wpPassword, wpInfoBefore, wpUserInfo,
          wpSelectDir, wpSelectProgramGroup, wpInfoAfter:
            Result := True;
        end;
end;

// ****************************************************************************
// Name:    ApachePathParent
// Purpose: Returns the path of Apache parent folder.
function ApachePathParent(): String;
var
    sApachePathParent: String;
    iLengthString: Integer;
    iPosK: Integer;
begin

    if RegKeyExists(HKLM, REG_KEY_APACHE_SERVICE) and UsingWinNT then
    begin
        // Set g_sApachePathBin
        RegQueryStringValue(HKLM, REG_KEY_APACHE_SERVICE,
                            'ImagePath', sApachePathParent);

        // Remove the run command and strip quotes away from g_sApachePathBin
        iPosK := Pos('-k', sApachePathParent);
        iLengthString := Length(sApachePathParent);
        Delete(sApachePathParent, iLengthString - (iLengthString - iPosK), iPosK);
        sApachePathParent := RemoveQuotes(sApachePathParent);

        // Strip basename twice so only the Apache parent path is left
        sApachePathParent := ExtractFileDir(sApachePathParent);
        sApachePathParent := ExtractFileDir(sApachePathParent);
    end;

    // Function variables
    Result := sApachePathParent;
end;

// ****************************************************************************
// Name:    ApacheTask
// Purpose: Decide if we should handle the Apache Server or not
function ApacheTask(): Boolean;
begin
    Result:= UsingWinNT and not (ApachePathParent = '');
end;

// ****************************************************************************
// Name:    ApacheBinFound
// Purpose: Checks if bin\apache.exe excists in Apache's parent folder .
//          Returns True if Yes and False if No
function ApacheBinFound(): Boolean;
var
    sApacheBinary: String;
begin
    sApacheBinary := ApachePathParent() + '\bin\apache.exe';

    if FileExists(sApacheBinary) then
    begin
        Result:= True;
    end else begin
        Result:= False;
    end;
end;

// ****************************************************************************
// Name:    ApacheServiceUninstall
// Purpose: Stopping and uninstalling the Apache Service
procedure ApacheServiceUninstall();
var
    bRetVal: Boolean;
    ErrorCode: Integer;
begin
    // Stop and uninstall the Apache service
    bRetVal := InstExec('cmd.exe', '/C apache -k stop', g_sApachePathBin,
                             True, False, SW_HIDE, ErrorCode);
    bRetVal := InstExec('cmd.exe', '/C apache -k uninstall', g_sApachePathBin,
                             True, False, SW_HIDE, ErrorCode);
end;

// ****************************************************************************
// Name:    ApacheServiceInstall
// Purpose: Installing and starting the Apache Service
procedure ApacheServiceInstall();
var
    bRetVal: Boolean;
    ErrorCode: Integer;
begin
    // Install and start the Apache service
	 	bRetVal := InstExec('cmd.exe', '/C apache -k install', g_sApachePathBin,
                        True, False, SW_HIDE, ErrorCode);

    bRetVal := InstExec('cmd.exe', '/C apache -k start', g_sApachePathBin,
                        True, False, SW_HIDE, ErrorCode);
end;


// ****************************************************************************
// Name:    ApacheModuleStatus
// Purpose: Identifying if a module is in a string and returning it's status
Function ApacheModuleStatus(sLine: String): Integer;
var
	iStatus: Integer;
	iPosSharp, iPosModule: Integer;
begin
    iStatus:= STATUS_NONE;
    iPosSharp := Pos('#', sLine);
    iPosModule := Pos('LoadModule ', sLine);

    if Pos('foo_module ', sLine) = 0 then
    begin
        if (iPosSharp > 0) and (iPosModule > iPosSharp) then
        begin
            iStatus := STATUS_DISABLED;
        end else
        begin
            iStatus := STATUS_ENABLED;
        end;
    end;

    Result := iStatus;
end;

// ****************************************************************************
// Name:    ApacheModuleName
// Purpose: Extracting and returning a module name from a string
Function ApacheModuleName(sLine: String): String;
var
    iPosModNameStart, iPosModNameEnd: Integer;
    iCharNum: Integer;
    sModuleName : String;
begin
    iPosModNameStart := (Pos('modules/mod_', sLine) + 12);
    iPosModNameEnd := (Pos('.so', sLine) - 1);

    sModuleName := '';
    iCharNum := iPosModNameStart;

    for iCharNum := iPosModNameStart to iPosModNameEnd do
    begin
        sModuleName := sModuleName + StrGet(sLine, iCharNum);
    end;

    sModuleName := sModuleName + '_module';

    Result := sModuleName;
end;

// ****************************************************************************
// Name:    ApacheConfFileEdit
// Purpose: Checking if the httpd.conf (Subversion related modules) file
//          if needed.
procedure ApacheConfFileEdit(aHttpdConf: TArrayOfString;
                             iPosFileModules, iPosFileModulesPost,
                             iPosModDav, iStatusModDav,
                             iPosModDavSvn, iStatusModDavSvn,
                             iPosModAuthzSvn, iStatusModAuthzSvn: Integer);
var
    sConfFileName, sTimeString: String;
    sLoadModDav, sLoadModDavSvn, sLoadModAuthzSvn: String;

begin
    sConfFileName := g_sApachePathConf + '\httpd.conf';
    sTimeString := GetDateTimeString('yyyy/mm/dd hh:mm:ss', '-', ':');

    sLoadModDav := 'LoadModule dav_module modules/mod_dav.so';
    sLoadModDavSvn := 'LoadModule dav_svn_module modules/mod_dav_svn.so';
    sLoadModAuthzSvn := 'LoadModule authz_svn_module modules/mod_authz_svn.so';

    //Backup the current httpd.conf
    FileCopy (sConfFileName, sConfFileName + '-svn-' + sTimeString + '.bak', False);

    // Add the modules if they're not there
    if (iStatusModDav = STATUS_NONE) then
    begin
        if iPosModDav = 0 then
        begin
            iPosModDav := iPosFileModules + 10;
        end;

        aHttpdConf[iPosModDav] := aHttpdConf[iPosModDav] + #13#10 + sLoadModDav;
    end;

    if (iStatusModDavSvn = STATUS_NONE) then
        aHttpdConf[iPosFileModulesPost -1] := aHttpdConf[iPosFileModulesPost -1] + #13#10 +
                                              sLoadModDavSvn;

    if (iStatusModAuthzSvn = STATUS_NONE) then
        aHttpdConf[iPosFileModulesPost -1] := aHttpdConf[iPosFileModulesPost -1] + #13#10 +
                                              sLoadModAuthzSvn;

    // Enable modules if disabled ********************************
    if (iStatusModDav = STATUS_DISABLED) then
        aHttpdConf[iPosModDav] := sLoadModDav;

    if (iStatusModDavSvn = STATUS_DISABLED) then
        aHttpdConf[iPosModDavSvn] := sLoadModDavSvn;

    if (iStatusModAuthzSvn = STATUS_DISABLED) then
        aHttpdConf[iPosModAuthzSvn] := sLoadModAuthzSvn;

    SaveStringsToFile(sConfFileName, aHttpdConf, False);
end;

// ****************************************************************************
// Name:    ApacheConfFileHandle
// Purpose: Checking if the httpd.conf (Subversion related modules) file should
//          be edited and sets some data for further proccessing if needed.
procedure ApacheConfFileHandle();
var
    aHttpdConf : TArrayOfString;

    sConfFileName: String;
    sCurrentLine: String;

    iLineNum, iArrayLen: Integer;

    iPosFileModules, iPosFileModulesPost : Integer;
    iPosModDav, iPosModDavSvn, iPosModAuthzSvn: Integer;
    iStatusModDav, iStatusModDavSvn, iStatusModAuthzSvn: Integer;
begin
    iStatusModDav := STATUS_NONE;
    iStatusModDavSvn := STATUS_NONE;
    iStatusModAuthzSvn := STATUS_NONE;

    sConfFileName:= g_sApachePathConf + '\httpd.conf';

    //Load the httpd.conf to the aHttpdConf array  and init vars
    LoadStringsFromFile(sConfFileName, aHttpdConf);

    iArrayLen := GetArrayLength(aHttpdConf)-1;

    // Check httpd.conf line by line
    for iLineNum := 0 to iArrayLen do
    begin
        sCurrentLine := aHttpdConf[iLineNum];

        // Get module status and file data with help of the modules
        if (Pos('LoadModule ', sCurrentLine) > 0 ) then
        begin
            if (ApacheModuleStatus(sCurrentLine) > 0) then
            begin
                if iPosFileModules = 0 then
                begin
                    iPosFileModules := iLineNum;
                end;

                iPosFileModulesPost := iLineNum + 1;
            end;

            //Decide placements and status of modules --------

            // dav_module: If we (for some reason) don't find dav_module then
            // we'll try to set the placement with help of cgi_module and make
            // sure that we do it _before_ a dav_fs_module.
            if ApacheModuleName(sCurrentLine) = 'dav_module' then
            begin
                iPosModDav := iLineNum;
                iStatusModDav := ApacheModuleStatus(sCurrentLine);
            end;

            if (ApacheModuleName(sCurrentLine) = 'cgi_module') and
               ((iStatusModDav = STATUS_NONE) and (iPosModDav = 0)) then
                   iPosModDav := iLineNum + 1;

            if (ApacheModuleName(sCurrentLine) = 'dav_fs_module') and
              (iStatusModDav = STATUS_NONE) then
                   iPosModDav := iLineNum - 1;

            // dav_svn_module:
            if ApacheModuleName(sCurrentLine) = 'dav_svn_module' then
            begin
                iPosModDavSvn := iLineNum;
                iStatusModDavSvn := ApacheModuleStatus(sCurrentLine);
            end;

            // authz_svn_module:
            if ApacheModuleName(sCurrentLine) = 'authz_svn_module' then
            begin
                iPosModAuthzSvn := iLineNum;
                iStatusModAuthzSvn := ApacheModuleStatus(sCurrentLine);
            end;
        end;
    end;

    // Edit httpd.conf if needed.
    if (iStatusModDav + iStatusModDavSvn + iStatusModAuthzSvn) <>
      (STATUS_ENABLED * 3) then
    begin
        ApacheConfFileEdit (aHttpdConf,
                            iPosFileModules, iPosFileModulesPost,
                            iPosModDav, iStatusModDav,
                            iPosModDavSvn, iStatusModDavSvn,
                            iPosModAuthzSvn, iStatusModAuthzSvn);
    end;
end;

// ****************************************************************************
// Name:    ApacheCopyModules
// Purpose: Extracting Apache's modules and the Berkeley DB from the
//          installation file and copy them to Apache's module directory
procedure ApacheCopyModules();
var
    sTPathTmp: String;
begin
    sTPathTmp := ExpandConstant('{tmp}');
    // extract the files from the setup to the current IS Temp folder
    ExtractTemporaryFile('libdb42.dll');
    ExtractTemporaryFile('mod_dav_svn.so');
    ExtractTemporaryFile('mod_authz_svn.so');
    ExtractTemporaryFile('intl.dll');

    //Copy the files from the temp dir to Apache's module foder
    FileCopy (sTPathTmp + '\libdb42.dll', g_sApachePathModules + '\libdb42.dll', False);
    FileCopy (sTPathTmp + '\mod_dav_svn.so', g_sApachePathModules + '\mod_dav_svn.so', False);
    FileCopy (sTPathTmp + '\mod_authz_svn.so', g_sApachePathModules + '\mod_authz_svn.so', False);
	FileCopy (sTPathTmp + '\intl.dll', g_sApachePathModules + '\intl.dll', False);
end;

// ****************************************************************************
// Name:    ApacheVersion
// Purpose: Returns apache.exe's version with the last number stripped.
function ApacheVersion(): String;
var
    sApacheVersion: String;
begin
    GetVersionNumbersString(g_sApachePathBin + '\apache.exe' ,sApacheVersion);
    Delete(sApacheVersion, 7, 2);
    Result := sApacheVersion;
end;

// ****************************************************************************
// Name:    VerifyApache
// Purpose: Finding/Setting Apache paths and version info
procedure VerifyApache();
var
    sMsg: String;
    sApacheVersion: String;
begin
    g_bHandleApache := True;

    // Set/check the Apache paths
    g_sApachePath := ApachePathParent;

    // apache.exe
    if g_sApachePathBin = '' then
        g_sApachePathBin := g_sApachePath + '\bin';

    if not FileExists(g_sApachePathBin + '\apache.exe') then
    begin
        sMsg := 'Could not find ''apache.exe'' in the system. Please, browse' +
                ' to the folder where the Apache binary is.';
        BrowseForFolder(sMsg , g_sApachePathBin, false);
    end;

    // httpd.conf
    if g_sApachePathConf = '' then
        g_sApachePathConf := g_sApachePath + '\conf';

    if not FileExists(g_sApachePathConf + '\httpd.conf') then
    begin
        sMsg := 'Could not find ''httpd.conf'' in the system. Please, browse' +
                ' to the folder where configuration file is.';
        BrowseForFolder(sMsg, g_sApachePathConf, false);
    end;

    // Modules folder
    if g_sApachePathModules = '' then
        g_sApachePathModules := g_sApachePath + '\modules';

    if not DirExists(g_sApachePathModules) then
    begin
        sMsg := 'Could not find the ''modules'' folder in the system. Please,' +
                ' browse to the folder where the Apache modules is.';
        BrowseForFolder(sMsg, g_sApachePathModules, false);
    end;

    // Check that we have the required Apache version and warn the user if
    // needed
    sApacheVersion := ApacheVersion;

    if CompareStr(sApacheVersion, APACHE_VER_MIN) < 0 then
    begin
        sMsg :=
            'WARNING: Apache http server version ' + sApacheVersion  + ' is detected and the'    + #13#10 +
            'Subversion modules are built for the ' + APACHE_VER_MIN + ' version of the server.' + #13#10#13#10 +
            'You are strongly encouraged to quit this setup and upgrade your'                    + #13#10 +
            'apache server first, or go back to this setup''s ''Additional Tasks'''              + #13#10 +
            'dialog box and uncheck the task of installing the Apache modules.'                  + #13#10;

            MsgBox(sMsg, mbError, MB_OK);
    end;
end;

// ****************************************************************************
// Name:    ShFolderDllNotFound
// Purpose: Checks if FILE_SHFOLDERDLL does not exist.
//          Returns True if missing and False if present.
function ShFolderDllNotFound(): Boolean;
var
    sSysDir: String;
begin
    sSysDir := ExpandConstant('{sys}');

    if FileExists(sSysDir + '\' + FILE_SHFOLDERDLL) then
    begin
        g_bShFolderNotFound := False;
    end else begin
        g_bShFolderNotFound := True;
    end;

    Result:= g_bShFolderNotFound;
end;

// ****************************************************************************
// Name:    SysFilesDownLoadInfo
// Purpose: Informs the user about missing Windows system file(s).
Procedure SysFilesDownLoadInfo;
var
    sSysFiles: String;
    sItThem: String;
    sFile: string;
    sDocument: string;
    sMsg: String;
begin
    sItThem := ' it';
    sFile := ' file';
    sDocument := ' document';

    if (g_bMsVcpNotFound and g_bShFolderNotFound) then
    begin
        sSysfiles := FILE_MSVCPDLL +  ' and ' + FILE_SHFOLDERDLL;
        sItThem := ' them';
        sFile := ' files';
        sDocument := ' documents';
    end;

    if (g_bMsVcpNotFound and not g_bShFolderNotFound) then
        sSysfiles := FILE_MSVCPDLL;

    if (g_bShFolderNotFound and not g_bMsVcpNotFound) then
        sSysfiles := FILE_SHFOLDERDLL;

    sMsg :='The' + sFile + ' ' + sSysFiles + ' was not found in the system.'                 + #13#10#13#10 +
           'Please, go to the Subversion entry in the Start Menu after the installation and' + #13#10 +
           'read the ''Download and install''' + sDocument + ' for ' + sSysfiles + '.'       + #13#10#13#10 +
           'Subversion will not work without this' + sFile + '.'                             + #13#10#13#10;

    MsgBox(sMsg, mbInformation, MB_OK);
end;

// ****************************************************************************
// Name:    VCRuntimeNotFound
// Purpose: Checks if FILE_MSVCPDLL does not exist.
//          Returns True if missing and False if present.
function VCRuntimeNotFound(): Boolean;
var
    sSysDir: String;
begin
    sSysDir := ExpandConstant('{sys}');

    if FileExists(sSysDir + '\' + FILE_MSVCPDLL) then
    begin
        g_bMsVcpNotFound := False;
    end else begin
        g_bMsVcpNotFound := True;
    end;

    Result:= g_bMsVcpNotFound;
end;

// ****************************************************************************
// The rest is build-in functions/events. (See Inno help file for  info about
// function/event names).
function InitializeSetup(): Boolean;
begin
    //Initialize some global variables
    g_bMsVcpNotFound := VCRuntimeNotFound;
    g_bShFolderNotFound := ShFolderDllNotFound;
    g_bHandleApache:= False;
    Result := True;
end;

procedure CurPageChanged(CurStep: Integer);
begin
    case CurStep of
        wpReady:      // Event after selected tasks
            if (ShouldProcessEntry('', 'apachehandler') = srYes) then
                VerifyApache;

        wpInstalling: // Event before setup is copying destination files
            if g_bHandleApache then
            begin
                ApacheServiceUninstall;
                ApacheCopyModules;
            end;
    end;
end;

procedure CurStepChanged(CurStep: Integer);
begin
    // Event after setup has copyed destination files
    if (CurStep = wpInfoBefore) and g_bHandleApache then
    begin;
        ApacheConfFileHandle;
        ApacheServiceInstall;
    end;
end;

function NextButtonClick(CurPage: Integer): Boolean;
begin
    if (CurPage = wpSelectComponents) then
        if (g_bMsVcpNotFound or g_bShFolderNotFound) then
            SysFilesDownLoadInfo();

    Result := True;
end;

