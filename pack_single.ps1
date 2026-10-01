param(
	[string]$Dist = "V:\pickle dayz\tech\smcr_editor\dist",
	[string]$InputExe = "V:\pickle dayz\tech\smcr_editor\dist\smcr_editor.exe",
	[string]$OutputExe = "V:\pickle dayz\tech\smcr_editor\smcr_editor_single.exe",
	[string]$OutEvb = "P:\Pickle_temp\enigma\pack.evb"
)

function Escape-Xml([string]$s) {
	$s -replace '&','&amp;' -replace '<','&lt;' -replace '>','&gt;'
}

function Write-FileNode([string]$realPath, [string]$name) {
	@"
			<File>
				<Type>2</Type>
				<Name>$(Escape-Xml $name)</Name>
				<File>$(Escape-Xml $realPath)</File>
				<ActiveX>false</ActiveX>
				<ActiveXInstall>false</ActiveXInstall>
				<Action>0</Action>
				<OverwriteDateTime>false</OverwriteDateTime>
				<OverwriteAttributes>false</OverwriteAttributes>
				<PassCommandLine>false</PassCommandLine>
			</File>
"@
}

function Write-DirNode([string]$realDir, [string]$name) {
	$inner = ''
	Get-ChildItem -LiteralPath $realDir | Sort-Object Name | ForEach-Object {
		if ($_.PSIsContainer) {
			$inner += Write-DirNode $_.FullName $_.Name
		} elseif ($_.FullName -ne $InputExe) {
			$inner += (Write-FileNode $_.FullName $_.Name) + "`n"
		}
	}
	@"
			<File>
				<Type>3</Type>
				<Name>$(Escape-Xml $name)</Name>
				<Action>0</Action>
				<OverwriteDateTime>false</OverwriteDateTime>
				<OverwriteAttributes>false</OverwriteAttributes>
				<Files>
$inner				</Files>
			</File>
"@
}

$rootFiles = ''
Get-ChildItem -LiteralPath $Dist | Sort-Object Name | ForEach-Object {
	if ($_.PSIsContainer) {
		$rootFiles += Write-DirNode $_.FullName $_.Name + "`n"
	} elseif ($_.FullName -ne $InputExe) {
		$rootFiles += (Write-FileNode $_.FullName $_.Name) + "`n"
	}
}

$evb = @"
<?xml encoding="utf-16"?>
<>
	<InputFile>$(Escape-Xml $InputExe)</InputFile>
	<OutputFile>$(Escape-Xml $OutputExe)</OutputFile>
	<Files>
		<Enabled>true</Enabled>
		<DeleteExtractedOnExit>true</DeleteExtractedOnExit>
		<CompressFiles>true</CompressFiles>
		<Files>
			<File>
				<Type>3</Type>
				<Name>%DEFAULT FOLDER%</Name>
				<Action>0</Action>
				<OverwriteDateTime>false</OverwriteDateTime>
				<OverwriteAttributes>false</OverwriteAttributes>
				<Files>
$rootFiles				</Files>
			</File>
		</Files>
	</Files>
	<Registries>
		<Enabled>false</Enabled>
		<Registries>
			<Registry>
				<Type>1</Type>
				<Virtual>true</Virtual>
				<Name>Classes</Name>
				<ValueType>0</ValueType>
				<Value/>
				<Registries/>
			</Registry>
			<Registry>
				<Type>1</Type>
				<Virtual>true</Virtual>
				<Name>User</Name>
				<ValueType>0</ValueType>
				<Value/>
				<Registries/>
			</Registry>
			<Registry>
				<Type>1</Type>
				<Virtual>true</Virtual>
				<Name>Machine</Name>
				<ValueType>0</ValueType>
				<Value/>
				<Registries/>
			</Registry>
			<Registry>
				<Type>1</Type>
				<Virtual>true</Virtual>
				<Name>Users</Name>
				<ValueType>0</ValueType>
				<Value/>
				<Registries/>
			</Registry>
			<Registry>
				<Type>1</Type>
				<Virtual>true</Virtual>
				<Name>Config</Name>
				<ValueType>0</ValueType>
				<Value/>
				<Registries/>
			</Registry>
		</Registries>
	</Registries>
	<Packaging>
		<Enabled>false</Enabled>
	</Packaging>
	<Options>
		<ShareVirtualSystem>false</ShareVirtualSystem>
		<MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>
		<AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>
	</Options>
</>
"@

[IO.File]::WriteAllText($OutEvb, $evb, (New-Object Text.UnicodeEncoding($false, $true)))
"wrote $OutEvb ($((Get-Item $OutEvb).Length) bytes)"
