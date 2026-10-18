-- GlideMix Trainer: the Desktop app. Asks what to do, which folders and how
-- hard to work, then runs run_trainer.sh in Terminal (progress shows there;
-- closing the window stops it, and the next run continues where it stopped).
-- Build: osacompile -o "$HOME/Desktop/GlideMix Trainer.app" "GlideMix Trainer.applescript"

property repoTrainer : "/Users/chris/Developer/audacity4/tools/trainer"
property defaultFolder : "/Volumes/Macintosh SD/MyTunesCollection"

on run
	set taskChoice to choose from list {"Phrase Detector — retrain on rekordbox's phrases", "Fingerprints — \"sounds like\" for every track and phrase", "Both (phrases first)"} with title "GlideMix Trainer" with prompt "What should it do tonight?" default items {"Phrase Detector — retrain on rekordbox's phrases"}
	if taskChoice is false then return
	set taskChoice to item 1 of taskChoice
	if taskChoice starts with "Phrase" then
		set tasks to {"phrases"}
	else if taskChoice starts with "Fingerprints" then
		set tasks to {"fingerprints"}
	else
		set tasks to {"phrases", "fingerprints"}
	end if

	set folderAnswer to button returned of (display dialog "Which music?" & return & return & "Default: " & defaultFolder buttons {"Cancel", "Choose folders…", "Use default"} default button "Use default" with title "GlideMix Trainer")
	if folderAnswer is "Use default" then
		set folderArgs to quoted form of defaultFolder
	else
		set picked to choose folder with prompt "Choose one or more music folders (subfolders are included):" with multiple selections allowed
		set folderArgs to ""
		repeat with f in picked
			set folderArgs to folderArgs & " " & quoted form of (POSIX path of f)
		end repeat
	end if

	set levelChoice to choose from list {"Low — 2 tracks at a time, the Mac stays fully usable", "Med — 4 at a time", "High — 8 at a time, fastest (best overnight)"} with title "GlideMix Trainer" with prompt "How hard should it work?" default items {"High — 8 at a time, fastest (best overnight)"}
	if levelChoice is false then return
	set levelWord to word 1 of (item 1 of levelChoice)
	if levelWord is "Low" then
		set level to "low"
	else if levelWord is "Med" then
		set level to "med"
	else
		set level to "high"
	end if

	set runner to quoted form of (repoTrainer & "/run_trainer.sh")
	set cmd to ""
	repeat with t in tasks
		if cmd is not "" then set cmd to cmd & " && "
		set cmd to cmd & runner & " " & t & " " & level & " " & folderArgs
	end repeat
	tell application "Terminal"
		activate
		do script "clear; echo 'GlideMix Trainer — close this window to stop (it continues where it stopped next time)'; " & cmd
	end tell
end run
