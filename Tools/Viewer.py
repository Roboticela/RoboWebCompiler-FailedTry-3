import os
import re
import subprocess
import sys
import tkinter
from tkinter import ttk


KEYWORD_COLOR = "#0550ae"
STRING_COLOR = "#0a7f32"
COMMENT_COLOR = "#6a737d"
ATTRIBUTE_COLOR = "#953800"
TEXT_BACKGROUND = "#ffffff"
TEXT_FOREGROUND = "#1a1a1a"
ERROR_COLOR = "#9d1c1c"
GUTTER_COLOR = "#f4f4f4"

JS_KEYWORDS = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do",
    "else", "export", "extends", "false", "finally", "for", "function", "if", "import", "in",
    "instanceof", "let", "new", "null", "return", "static", "super", "switch", "this", "throw",
    "true", "try", "typeof", "undefined", "var", "void", "while", "yield", "async", "await",
}


def ProjectRoot():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ConfigureTags(editor):
    editor.tag_configure("keyword", foreground=KEYWORD_COLOR)
    editor.tag_configure("string", foreground=STRING_COLOR)
    editor.tag_configure("comment", foreground=COMMENT_COLOR)
    editor.tag_configure("attribute", foreground=ATTRIBUTE_COLOR)


def ApplyRange(editor, start, end, tag):
    if end > start:
        editor.tag_add(tag, "1.0+%dc" % start, "1.0+%dc" % end)


def HighlightHtml(editor, source):
    for tag in ("keyword", "string", "comment", "attribute"):
        editor.tag_remove(tag, "1.0", "end")
    for match in re.finditer(r"<!--.*?-->|<!DOCTYPE[^>]*>", source, re.IGNORECASE | re.DOTALL):
        ApplyRange(editor, match.start(), match.end(), "comment")
    for match in re.finditer(r"</?[A-Za-z!][^<>]*>", source):
        ApplyRange(editor, match.start(), match.end(), "keyword")
        body = match.group(0)
        for attribute in re.finditer(r"([A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*(\"[^\"]*\"|'[^']*')", body):
            ApplyRange(editor, match.start() + attribute.start(1), match.start() + attribute.end(1), "attribute")
            ApplyRange(editor, match.start() + attribute.start(2), match.start() + attribute.end(2), "string")


def HighlightCss(editor, source):
    for tag in ("keyword", "string", "comment", "attribute"):
        editor.tag_remove(tag, "1.0", "end")
    for match in re.finditer(r"/\*.*?\*/", source, re.DOTALL):
        ApplyRange(editor, match.start(), match.end(), "comment")
    for match in re.finditer(r"\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", source):
        ApplyRange(editor, match.start(), match.end(), "string")
    for match in re.finditer(r"(^|[{};])\s*([A-Za-z-]+)\s*:", source):
        ApplyRange(editor, match.start(2), match.end(2), "attribute")
    for match in re.finditer(r"[.#]?[A-Za-z_][-A-Za-z0-9_]*\s*(?=\{)", source):
        ApplyRange(editor, match.start(), match.end(), "keyword")


def HighlightJs(editor, source):
    for tag in ("keyword", "string", "comment", "attribute"):
        editor.tag_remove(tag, "1.0", "end")
    for match in re.finditer(r"/\*.*?\*/|//[^\n]*", source, re.DOTALL):
        ApplyRange(editor, match.start(), match.end(), "comment")
    for match in re.finditer(r"\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`", source):
        ApplyRange(editor, match.start(), match.end(), "string")
    for match in re.finditer(r"\b[A-Za-z_$][A-Za-z0-9_$]*\b", source):
        if match.group(0) in JS_KEYWORDS:
            ApplyRange(editor, match.start(), match.end(), "keyword")


def Highlight(editor, language):
    source = editor.get("1.0", "end-1c")
    if language == "html":
        HighlightHtml(editor, source)
    elif language == "css":
        HighlightCss(editor, source)
    else:
        HighlightJs(editor, source)


class CodeEditor(tkinter.Frame):
    def __init__(self, parent, language):
        super().__init__(parent, background=TEXT_BACKGROUND)
        self.language = language
        self.refreshing = False
        self.editor = tkinter.Text(
            self,
            wrap="none",
            undo=True,
            background=TEXT_BACKGROUND,
            foreground=TEXT_FOREGROUND,
            insertbackground=TEXT_FOREGROUND,
            font=("Consolas", 12),
            padx=8,
            pady=8,
            borderwidth=0,
            highlightthickness=1,
            highlightbackground="#c8c8c8",
        )
        scroll_y = tkinter.Scrollbar(self, orient="vertical", command=self.editor.yview)
        scroll_x = tkinter.Scrollbar(self, orient="horizontal", command=self.editor.xview)
        self.editor.configure(yscrollcommand=scroll_y.set, xscrollcommand=scroll_x.set)
        self.editor.grid(row=0, column=0, sticky="nsew")
        scroll_y.grid(row=0, column=1, sticky="ns")
        scroll_x.grid(row=1, column=0, sticky="ew")
        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)
        ConfigureTags(self.editor)
        self.editor.bind("<<Modified>>", self.OnModified)
        self.editor.bind("<KeyRelease>", self.OnKey)

    def OnModified(self, _event):
        if self.refreshing:
            return
        if self.editor.edit_modified():
            self.refreshing = True
            Highlight(self.editor, self.language)
            self.editor.edit_modified(False)
            self.refreshing = False

    def OnKey(self, event):
        if event.state & 0x4 and event.keysym == "Return":
            self.event_generate("<<RunCode>>")
            return "break"
        return None

    def Text(self):
        return self.editor.get("1.0", "end-1c")

    def SetText(self, value):
        self.refreshing = True
        self.editor.delete("1.0", "end")
        self.editor.insert("1.0", value)
        Highlight(self.editor, self.language)
        self.editor.edit_modified(False)
        self.refreshing = False

    def FocusLine(self, line):
        self.editor.see("%d.0" % line)
        self.editor.tag_remove("sel", "1.0", "end")
        self.editor.tag_add("sel", "%d.0" % line, "%d.end" % line)
        self.editor.focus_set()


class Studio:
    def __init__(self, html_path, width, height):
        self.root_dir = ProjectRoot()
        self.html_path = html_path
        self.viewport_width = width
        self.viewport_height = height
        self.page_process = None
        self.photo = None
        self.sending = False
        self.editor_window = tkinter.Tk()
        self.editor_window.title("RoboWebCompiler — Code")
        self.editor_window.geometry("760x820+40+40")
        self.editor_window.configure(background=TEXT_BACKGROUND)
        self.preview_window = tkinter.Toplevel(self.editor_window)
        self.preview_window.title("RoboWebCompiler — Output")
        self.preview_window.geometry("%dx%d+820+40" % (width + 24, height + 48))
        self.preview_window.configure(background=TEXT_BACKGROUND)
        self.BuildEditor()
        self.BuildPreview()
        self.LoadSource()
        self.editor_window.protocol("WM_DELETE_WINDOW", self.Close)
        self.preview_window.protocol("WM_DELETE_WINDOW", self.Close)
        self.editor_window.after(150, self.Run)

    def BuildEditor(self):
        bar = tkinter.Frame(self.editor_window, background=GUTTER_COLOR)
        bar.pack(fill="x")
        run = tkinter.Button(bar, text="Run", command=self.Run, background=GUTTER_COLOR, relief="solid", borderwidth=1)
        run.pack(side="left", padx=8, pady=6)
        self.path_label = tkinter.Label(bar, text="", anchor="w", background=GUTTER_COLOR, foreground=TEXT_FOREGROUND)
        self.path_label.pack(side="left", fill="x", expand=True)
        notebook = ttk.Notebook(self.editor_window)
        notebook.pack(fill="both", expand=True, padx=8, pady=8)
        self.html_editor = CodeEditor(notebook, "html")
        self.css_editor = CodeEditor(notebook, "css")
        self.js_editor = CodeEditor(notebook, "js")
        notebook.add(self.html_editor, text="HTML")
        notebook.add(self.css_editor, text="CSS")
        notebook.add(self.js_editor, text="JS")
        self.notebook = notebook
        self.editors = {"html": self.html_editor, "css": self.css_editor, "js": self.js_editor}
        for editor in self.editors.values():
            editor.bind("<<RunCode>>", lambda _event: self.Run())
        self.diagnostics = tkinter.Text(
            self.editor_window,
            height=8,
            wrap="none",
            background=TEXT_BACKGROUND,
            foreground=TEXT_FOREGROUND,
            font=("Consolas", 10),
            borderwidth=0,
            highlightthickness=1,
            highlightbackground="#c8c8c8",
        )
        self.diagnostics.pack(fill="x", padx=8, pady=(0, 8))
        self.diagnostics.tag_configure("error", foreground=ERROR_COLOR)
        self.diagnostics.tag_configure("warning", foreground=ATTRIBUTE_COLOR)
        self.diagnostics.bind("<Double-Button-1>", self.OpenDiagnostic)

    def BuildPreview(self):
        self.canvas = tkinter.Canvas(
            self.preview_window,
            background=TEXT_BACKGROUND,
            highlightthickness=0,
            width=self.viewport_width,
            height=self.viewport_height,
        )
        self.canvas.pack(fill="both", expand=True)
        self.preview_status = tkinter.Label(
            self.preview_window, text="", anchor="w", background=GUTTER_COLOR, foreground=TEXT_FOREGROUND
        )
        self.preview_status.pack(fill="x")
        self.canvas.bind("<Button-1>", self.OnClick)
        self.canvas.bind("<MouseWheel>", self.OnWheel)
        self.preview_window.bind("<Key>", self.OnKey)
        self.canvas.bind("<Configure>", self.OnPreviewResize)

    def LoadSource(self):
        html = ""
        if self.html_path and os.path.isfile(self.html_path):
            with open(self.html_path, "r", encoding="utf-8-sig") as handle:
                html = handle.read()
            self.path_label.config(text=self.html_path)
        else:
            html = "<!DOCTYPE html>\n<html>\n<head><title>Page</title></head>\n<body>\n  <h1>Hello</h1>\n  <p>Edit HTML, CSS, and JS, then press Run.</p>\n</body>\n</html>\n"
            self.path_label.config(text="untitled.html")
        css = ""
        js = ""
        css_match = re.search(r"<style[^>]*>(.*?)</style>", html, re.IGNORECASE | re.DOTALL)
        js_match = re.search(r"<script[^>]*>(.*?)</script>", html, re.IGNORECASE | re.DOTALL)
        if css_match:
            css = css_match.group(1).strip("\n")
            html = html[: css_match.start()] + html[css_match.end() :]
        if js_match:
            js_match = re.search(r"<script[^>]*>(.*?)</script>", html, re.IGNORECASE | re.DOTALL)
        if js_match:
            js = js_match.group(1).strip("\n")
            html = html[: js_match.start()] + html[js_match.end() :]
        self.html_editor.SetText(html.strip() + "\n")
        self.css_editor.SetText(css + ("\n" if css else ""))
        self.js_editor.SetText(js + ("\n" if js else ""))

    def Compose(self):
        html = self.html_editor.Text().replace("\r\n", "\n")
        css = self.css_editor.Text().strip()
        js = self.js_editor.Text().strip()
        self.line_map = []
        if "<html" not in html.lower() and "<!doctype" not in html.lower() and "<body" not in html.lower():
            pieces = ["<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"]
            if css:
                pieces.append("<style>\n")
                pieces.append(css + "\n")
                pieces.append("</style>\n")
            pieces.append("</head>\n<body>\n")
            pieces.append(html if html.endswith("\n") else html + "\n")
            if js:
                pieces.append("<script>\n")
                pieces.append(js + "\n")
                pieces.append("</script>\n")
            pieces.append("</body>\n</html>\n")
            return "".join(pieces)
        if css:
            style = "<style>\n" + css + "\n</style>\n"
            if re.search(r"</head>", html, re.IGNORECASE):
                html = re.sub(r"</head>", style + "</head>", html, count=1, flags=re.IGNORECASE)
            else:
                html = style + html
        if js:
            script = "<script>\n" + js + "\n</script>\n"
            if re.search(r"</body>", html, re.IGNORECASE):
                html = re.sub(r"</body>", script + "</body>", html, count=1, flags=re.IGNORECASE)
            else:
                html += script
        return html

    def ShowDiagnostics(self, text):
        self.diagnostics.configure(state="normal")
        self.diagnostics.delete("1.0", "end")
        if text.strip() == "":
            self.diagnostics.insert("1.0", "Compiled.\n")
        else:
            for line in text.splitlines():
                tag = "error" if ": error:" in line else "warning" if ": warning:" in line else ""
                self.diagnostics.insert("end", line + "\n", tag)
        self.diagnostics.configure(state="disabled")

    def OpenDiagnostic(self, _event):
        line = self.diagnostics.get("current linestart", "current lineend")
        match = re.search(r":(\d+):(\d+):", line)
        if not match:
            return
        self.html_editor.FocusLine(int(match.group(1)))
        self.notebook.select(0)

    def StopPage(self):
        process = self.page_process
        self.page_process = None
        if process is None or process.poll() is not None:
            return
        try:
            if process.stdin is not None:
                process.stdin.write("quit\n")
                process.stdin.flush()
            process.wait(timeout=2)
        except Exception:
            process.kill()

    def Run(self):
        compiler = os.path.join(self.root_dir, "Build", "RoboWebCompiler.exe")
        if not os.path.isfile(compiler):
            self.ShowDiagnostics("RoboWebCompiler: error: compiler is missing. Run RoboWeb.bat build first.\n")
            return
        os.makedirs(os.path.join(self.root_dir, "Build"), exist_ok=True)
        source_path = os.path.join(self.root_dir, "Build", "Editor.html")
        with open(source_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(self.Compose())
        output_path = os.path.join(self.root_dir, "Build", "Page.exe")
        command = [
            compiler,
            source_path,
            "-o",
            output_path,
            "--source-dir",
            os.path.join(self.root_dir, "Source"),
            "--object-dir",
            os.path.join(self.root_dir, "Build", "Objects"),
        ]
        compiled = subprocess.run(command, capture_output=True, text=True)
        message = (compiled.stderr or "") + (compiled.stdout or "")
        self.ShowDiagnostics(message)
        if compiled.returncode != 0:
            self.preview_status.config(text="Compilation failed")
            return
        self.StopPage()
        frame_path = output_path + ".ppm"
        self.frame_path = frame_path
        self.page_process = subprocess.Popen(
            [output_path, "--interactive", "--out", frame_path, "--width", str(self.viewport_width), "--height", str(self.viewport_height)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.Send("viewport %d %d" % (self.viewport_width, self.viewport_height))

    def Send(self, command):
        process = self.page_process
        if process is None or process.poll() is not None or process.stdin is None or process.stdout is None:
            return
        if self.sending:
            return
        self.sending = True
        try:
            process.stdin.write(command + "\n")
            process.stdin.flush()
            status = ""
            title = ""
            while True:
                line = process.stdout.readline()
                if line == "":
                    break
                line = line.strip()
                if line.startswith("TITLE "):
                    title = line[6:]
                    self.preview_window.title("RoboWebCompiler — Output" if title == "" else "RoboWebCompiler — " + title)
                elif line.startswith("STATUS "):
                    status = line[7:]
                elif line.startswith("FRAME "):
                    parts = line.split()
                    if len(parts) >= 3:
                        self.viewport_width = int(parts[1])
                        self.viewport_height = int(parts[2])
                    break
            if os.path.isfile(self.frame_path):
                self.photo = tkinter.PhotoImage(file=self.frame_path.replace("\\", "/"))
                self.canvas.delete("frame")
                self.canvas.create_image(0, 0, anchor="nw", image=self.photo, tags="frame")
                self.canvas.configure(scrollregion=(0, 0, self.photo.width(), self.photo.height()))
            self.preview_status.config(text=status)
        finally:
            self.sending = False

    def OnClick(self, event):
        self.preview_window.focus_set()
        self.Send("click %d %d" % (event.x, event.y))

    def OnWheel(self, event):
        delta = -40 if event.delta > 0 else 40
        self.Send("scroll %d" % delta)

    def OnKey(self, event):
        if event.keysym == "BackSpace":
            self.Send("key backspace")
        elif event.char != "":
            self.Send("key " + event.char)

    def OnPreviewResize(self, event):
        if event.widget is not self.canvas or self.page_process is None or self.sending:
            return
        if abs(event.width - self.viewport_width) < 16 and abs(event.height - self.viewport_height) < 16:
            return
        if hasattr(self, "resize_job"):
            self.preview_window.after_cancel(self.resize_job)
        width = max(1, event.width)
        height = max(1, event.height)
        self.resize_job = self.preview_window.after(250, lambda: self.Send("viewport %d %d" % (width, height)))

    def Close(self):
        self.StopPage()
        self.editor_window.destroy()

    def Main(self):
        self.editor_window.mainloop()


def main():
    html_path = sys.argv[1] if len(sys.argv) > 1 else ""
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 960
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 640
    Studio(html_path, width, height).Main()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
