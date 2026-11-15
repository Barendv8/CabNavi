#define AppName "CabNavi"
#define AppVersion "1.3.1"
#define AppPublisher "CabNavi"
#define AppURL "https://github.com/CabNavi/CabNavi"

[Setup]
AppId={{7C2E0A5B-4D9B-4F3E-9C1A-2E5F1C0B7A31}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={code:GameDir}\bin\win_x64\plugins
DisableDirPage=no
DirExistsWarning=no
AppendDefaultDirName=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
OutputDir=..\build\installer
OutputBaseFilename=CabNavi-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
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
en.GameDirCaption=Game folder
en.GameDirDesc=Where is the game installed?
en.GameDirSub=Setup could not find Euro Truck Simulator 2 or American Truck Simulator automatically. Select a game folder (the one that contains bin\win_x64\eurotrucks2.exe or amtrucks.exe).
en.NotGameDir=That folder does not contain bin\win_x64\eurotrucks2.exe or bin\win_x64\amtrucks.exe. Please select the folder of one of the two games.
en.KeepData=Your trips, settings and tachograph state in %APPDATA%\CabNavi have been kept. Delete that folder yourself if you want them gone too.
nl.GameDirCaption=Spelmap
nl.GameDirDesc=Waar staat het spel?
nl.GameDirSub=Setup kon Euro Truck Simulator 2 en American Truck Simulator geen van beide automatisch vinden. Kies een spelmap (de map met bin\win_x64\eurotrucks2.exe of amtrucks.exe erin).
nl.NotGameDir=Die map bevat geen bin\win_x64\eurotrucks2.exe en ook geen bin\win_x64\amtrucks.exe. Kies de map van een van de twee spellen.
nl.KeepData=Je ritten, instellingen en tachograafstand in %APPDATA%\CabNavi zijn bewaard. Gooi die map zelf weg als je ook die kwijt wilt.

[Files]
Source: "..\build\Release\cabnavi.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\cabnavi.dll"; DestDir: "{code:SecondPluginDir}"; Check: HasSecondGame; Flags: ignoreversion
Source: "..\icons\*.png"; DestDir: "{userappdata}\CabNavi\icons"; Flags: ignoreversion
Source: "..\logo.png"; DestDir: "{userappdata}\CabNavi"; Flags: onlyifdoesntexist

[UninstallDelete]
Type: files; Name: "{userappdata}\CabNavi\icons\*.png"
Type: dirifempty; Name: "{userappdata}\CabNavi\icons"

[Code]
var
  GameDirPage: TInputDirWizardPage;
  FoundGameDir: String;

function IsGameDir(const Dir: String): Boolean;
begin
  Result := FileExists(AddBackslash(Dir) + 'bin\win_x64\eurotrucks2.exe')
         or FileExists(AddBackslash(Dir) + 'bin\win_x64\amtrucks.exe');
end;

function FindInSteamLibraries(const SteamPath, FolderName: String): String;
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
      Candidate := AddBackslash(Line) + 'steamapps\common\' + FolderName;
      if IsGameDir(Candidate) then begin Result := Candidate; Exit; end;
    end;
  end;
end;

function DetectOneGame(const FolderName: String): String;
var
  SteamPath, Candidate: String;
  Drives: array of String;
  I: Integer;
begin
  Result := '';
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamPath) then
  begin
    StringChangeEx(SteamPath, '/', '\', True);
    Candidate := AddBackslash(SteamPath) + 'steamapps\common\' + FolderName;
    if IsGameDir(Candidate) then begin Result := Candidate; Exit; end;
    Result := FindInSteamLibraries(SteamPath, FolderName);
    if Result <> '' then Exit;
  end;
  SetArrayLength(Drives, 6);
  Drives[0] := ExpandConstant('{commonpf32}') + '\Steam\steamapps\common\' + FolderName;
  Drives[1] := ExpandConstant('{commonpf}') + '\Steam\steamapps\common\' + FolderName;
  Drives[2] := 'C:\Steam\steamapps\common\' + FolderName;
  Drives[3] := 'D:\Steam\steamapps\common\' + FolderName;
  Drives[4] := 'D:\SteamLibrary\steamapps\common\' + FolderName;
  Drives[5] := 'E:\SteamLibrary\steamapps\common\' + FolderName;
  for I := 0 to 5 do
    if IsGameDir(Drives[I]) then begin Result := Drives[I]; Exit; end;
end;

function DetectGameDir(): String;
begin
  Result := DetectOneGame('Euro Truck Simulator 2');
  if Result = '' then Result := DetectOneGame('American Truck Simulator');
end;

function SecondPluginDir(Param: String): String;
var
  Ets2, Ats, Primary: String;
begin
  Result := '';
  Ets2 := DetectOneGame('Euro Truck Simulator 2');
  Ats := DetectOneGame('American Truck Simulator');
  Primary := ExpandConstant('{app}');
  if (Ats <> '') and (Pos(Lowercase(Ats), Lowercase(Primary)) = 0) then
    Result := AddBackslash(Ats) + 'bin\win_x64\plugins'
  else if (Ets2 <> '') and (Pos(Lowercase(Ets2), Lowercase(Primary)) = 0) then
    Result := AddBackslash(Ets2) + 'bin\win_x64\plugins';
end;

function HasSecondGame(): Boolean;
begin
  Result := SecondPluginDir('') <> '';
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
var
  Tweede: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    Tweede := SecondPluginDir('');
    if Tweede <> '' then
      DeleteFile(AddBackslash(Tweede) + 'cabnavi.dll');
  end;
  if CurUninstallStep = usPostUninstall then
    MsgBox(CustomMessage('KeepData'), mbInformation, MB_OK);
end;
