import sublime
import sublime_plugin
import subprocess

class OpenQuickpickOnSelection(sublime_plugin.TextCommand):
    def run(self, edit):
        view = self.view
        sel = view.sel()
        last = sel[len(sel) - 1]
        for r in sel:
            b = r.a
            row, col = view.rowcol(b)
            if view.line_endings() == "Windows":
                b += row
            p = subprocess.Popen(["quickpick", "{}@{}".format(view.file_name(), b)], shell=True)
