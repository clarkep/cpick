QuickPickSublimeText: sublime plugin for the quickpick color picker.

To install, copy this folder to your Packages directory located at
	Windows: %APPDATA%\Sublime Text 3\Packages
	OS X: ~/Library/Application Support/Sublime Text 3\Packages
	Linux: ~/.config/sublime-text-3\Packages

The plugin defines the command open_quickpick_on_selection, but does not bind it to any shortcut. To bind
the command to alt+shift+c, for example, add this line to your Default.sublime-keymap located under
Packages/User:
	{ "keys": ["alt+shift+c"], "command": "open_quickpick_on_selection"},

Running the command will launch one instance of quickpick for each active selection.

The quickpick project is hosted at github.com/clarkep/quickpick
