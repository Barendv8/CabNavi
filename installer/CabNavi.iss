; ---------------------------------------------------------------------------
; CabNavi installer -- Inno Setup 6 script.
;
; Builds a Next-Next-Finish wizard that:
;   * finds Euro Truck Simulator 2 (Steam registry, all library folders,
;     the usual paths) and asks for the folder only if it cannot,
;   * copies cabnavi.dll into <game>\bin\win_x64\plugins\ (created if needed),
;   * puts the tab icons and the default logo in %APPDATA%\CabNavi\,
;   * registers an uninstaller under Apps & features that removes the plugin
;     and the icons but LEAVES your data (trips, settings, tachograph).
;
; Build: install Inno Setup 6 (jrsoftware.org), open this file, Compile.
; Expects the build output next to the script layout below (see [Files]).
; Nothing here runs, hooks or modifies the game; it only copies files.
; ---------------------------------------------------------------------------

#define AppName "CabNavi"
#define AppVersion "1.0.1"
#define AppPublisher "Weeda Transport"
#define AppURL "https://github.com/Barendv8/CabNavi"

[Setup]
AppId={{7C2E0A5B-4D9B-4F3E-9C1A-2E5F1C0B7A31}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
; The "install dir" is the game's plugin folder; the user sees and can change it.
DefaultDirName={code:GameDir}\bin\win_x64\plugins
DisableDirPage=no
DirExistsWarning=no
AppendDefaultDirName=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
OutputDir=..\build\installer
OutputBaseFilename=CabNavi-{#AppVersion}-setup
; SetupIconFile=..\docs\images\icon.ico   ; Inno needs .ico -- convert icon.png once (any online converter) and uncomment
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Run as the player, not as admin: the icons must land in THIS user's
; %APPDATA%, and Steam makes its game folders writable for normal users.
; Should the plugin folder refuse the copy anyway, Setup reports it and the
; user can rerun with "Run as administrator" (the override dialog allows it).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayName={#AppName} (TruckersMP plugin)
UninstallDisplayIcon={app}\cabnavi.dll
LicenseFile=..\LICENSE
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"

[CustomMessages]
en.GameDirCaption=Euro Truck Simulator 2 folder
en.GameDirDesc=Where is the game installed?
en.GameDirSub=Setup could not find Euro Truck Simulator 2 automatically. Select the game folder (the one that contains bin\win_x64\eurotrucks2.exe).
en.NotGameDir=That folder does not contain bin\win_x64\eurotrucks2.exe. Please select the Euro Truck Simulator 2 folder.
en.KeepData=Your trips, settings and tachograph state in %APPDATA%\CabNavi have been kept. Delete that folder yourself if you want them gone too.
nl.GameDirCaption=Map van Euro Truck Simulator 2
nl.GameDirDesc=Waar staat het spel?
nl.GameDirSub=Setup kon Euro Truck Simulator 2 niet automatisch vinden. Kies de spelmap (de map met bin\win_x64\eurotrucks2.exe erin).
nl.NotGameDir=Die map bevat geen bin\win_x64\eurotrucks2.exe. Kies de map van Euro Truck Simulator 2.
nl.KeepData=Je ritten, instellingen en tachograafstand in %APPDATA%\CabNavi zijn bewaard. Gooi die map zelf weg als je ook die kwijt wilt.

[Files]
; The plugin itself, from the CMake Release build.
Source: "..\build\Release\cabnavi.dll"; DestDir: "{app}"; Flags: ignoreversion
; Tab icons and default logo go to the user's AppData; never overwrite a logo
; the user put there themselves.
Source: "..\icons\*.png"; DestDir: "{userappdata}\CabNavi\icons"; Flags: ignoreversion
Source: "..\logo.png"; DestDir: "{userappdata}\CabNavi"; Flags: onlyifdoesntexist

[UninstallDelete]
; Only what we installed. User data stays.
Type: files; Name: "{userappdata}\CabNavi\icons\*.png"
Type: dirifempty; Name: "{userappdata}\CabNavi\icons"

[Code]
var
  GameDirPage: TInputDirWizardPage;
  FoundGameDir: String;

function IsGameDir(const Dir: String): Boolean;
begin
  Result := FileExists(AddBackslash(Dir) + 'bin\win_x64\eurotrucks2.exe');
end;

{ Steam's libraryfolders.vdf lists every library; each has "path" lines. }
function FindInSteamLibraries(const SteamPath: String): String;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Line, Candidate: String;
begin
  Result := '';
  if not LoadStringsFromFile(AddBackslash(SteamPath) + 'steamapps\libraryfolders.vdf', Lines) then Exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Trim(Lines[I]);
    P := Pos('"path"', Line);
    if P = 1 then
    begin
      Line := Trim(Copy(Line, 7, Length(Line)));
      Line := RemoveQuotes(Line);
      StringChangeEx(Line, '\\', '\', True);
      Candidate := AddBackslash(Line) + 'steamapps\common\Euro Truck Simulator 2';
      if IsGameDir(Candidate) then begin Result := Candidate; Exit; end;
    end;
  end;
end;

function DetectGameDir(): String;
var
  SteamPath, Candidate: String;
  Drives: array of String;
  I: Integer;
begin
  Result := '';
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamPath) then
  begin
    StringChangeEx(SteamPath, '/', '\', True);
    Candidate := AddBackslash(SteamPath) + 'steamapps\common\Euro Truck Simulator 2';
    if IsGameDir(Candidate) then begin Result := Candidate; Exit; end;
    Result := FindInSteamLibraries(SteamPath);
    if Result <> '' then Exit;
  end;
  SetArrayLength(Drives, 6);
  Drives[0] := ExpandConstant('{commonpf32}') + '\Steam\steamapps\common\Euro Truck Simulator 2';
  Drives[1] := ExpandConstant('{commonpf}') + '\Steam\steamapps\common\Euro Truck Simulator 2';
  Drives[2] := 'C:\Steam\steamapps\common\Euro Truck Simulator 2';
  Drives[3] := 'D:\Steam\steamapps\common\Euro Truck Simulator 2';
  Drives[4] := 'D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2';
  Drives[5] := 'E:\SteamLibrary\steamapps\common\Euro Truck Simulator 2';
  for I := 0 to 5 do
    if IsGameDir(Drives[I]) then begin Result := Drives[I]; Exit; end;
end;

function GameDir(Param: String): String;
begin
  if FoundGameDir <> '' then Result := FoundGameDir
  else if (GameDirPage <> nil) and (GameDirPage.Values[0] <> '') then Result := GameDirPage.Values[0]
  else Result := ExpandConstant('{commonpf32}') + '\Steam\steamapps\common\Euro Truck Simulator 2';
end;

procedure InitializeWizard();
begin
  FoundGameDir := DetectGameDir();
  GameDirPage := CreateInputDirPage(wpWelcome,
    CustomMessage('GameDirCaption'), CustomMessage('GameDirDesc'), CustomMessage('GameDirSub'),
    False, '');
  GameDirPage.Add('');
  GameDirPage.Values[0] := GameDir('');
end;

{ Skip the question when the game was found; the normal directory page still
  shows the resulting plugin folder so the user can see (and change) it. }
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = GameDirPage.ID then Result := (FoundGameDir <> '');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = GameDirPage.ID then
  begin
    if not IsGameDir(GameDirPage.Values[0]) then
    begin
      MsgBox(CustomMessage('NotGameDir'), mbError, MB_OK);
      Result := False;
    end
    else
      WizardForm.DirEdit.Text := AddBackslash(GameDirPage.Values[0]) + 'bin\win_x64\plugins';
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    MsgBox(CustomMessage('KeepData'), mbInformation, MB_OK);
end;
