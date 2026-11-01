"""Progress UI for the existing device-code flow; never handles credentials."""
import queue
import threading

_current = None

class SignInCancelled(RuntimeError):
    pass

class SignInProgress:
    def __init__(self, code, url):
        self.code, self.url = code, url
        self.accepted = threading.Event()
        self.cancelled = threading.Event()
        self.ready = threading.Event()
        self.commands = queue.Queue()
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        self.ready.wait(10)
        if self.error or not self.ready.is_set():
            raise RuntimeError('Sign-in progress window could not open')

    def _run(self):
        try:
            import tkinter as tk
            from tkinter import ttk
            root = tk.Tk()
            root.title('Minecraft: Apple TV Edition')
            root.resizable(False, False)
            frame = ttk.Frame(root, padding=24)
            frame.pack(fill='both', expand=True)
            heading = ttk.Label(frame, text='Xbox sign-in required', font=('Segoe UI', 13, 'bold'))
            heading.pack(anchor='w', pady=(0,12))
            message = tk.StringVar(value=(f'Open {self.url} and enter:\n\n{self.code}\n\n'
                'Click OK to copy the code and open your normal browser. Paste it with Ctrl+V.\n\n'
                'Use the same account as the Xbox app and Microsoft Store.\n\n'
                'Keep this window open. Minecraft will start automatically after sign-in completes.'))
            ttk.Label(frame, textvariable=message, wraplength=460, justify='left').pack(anchor='w')
            progress = ttk.Progressbar(frame, mode='indeterminate', length=460)
            progress.pack(fill='x', pady=(18,12))
            buttons = ttk.Frame(frame);buttons.pack(fill='x')
            def cancel():
                self.cancelled.set();self.accepted.set();root.destroy()
            def accept():
                heading.configure(text='Waiting for sign-in...')
                message.set(f'Complete sign-in in your browser using code {self.code}.\n\n'
                    'Use the same account as the Xbox app and Microsoft Store.\n\n'
                    'Minecraft will open automatically when sign-in is verified. You can leave this window in the background.')
                ok.configure(state='disabled');progress.start(12);self.accepted.set()
            ok=ttk.Button(buttons,text='OK',command=accept);ok.pack(side='right')
            ttk.Button(buttons,text='Cancel',command=cancel).pack(side='right',padx=8)
            root.protocol('WM_DELETE_WINDOW',cancel)
            root.update_idletasks()
            root.geometry(f'+{max(0,(root.winfo_screenwidth()-root.winfo_width())//2)}+{max(0,(root.winfo_screenheight()-root.winfo_height())//2)}')
            def pump():
                try:
                    while True:
                        kind,text=self.commands.get_nowait()
                        if kind=='close':root.destroy();return
                        heading.configure(text=text)
                except queue.Empty:pass
                root.after(100,pump)
            root.after(100,pump);self.ready.set();root.mainloop()
        except Exception as error:
            self.error=type(error).__name__;self.cancelled.set();self.accepted.set();self.ready.set()

    def wait_for_ok(self):
        self.accepted.wait()
        self.check_cancelled()

    def check_cancelled(self):
        if self.cancelled.is_set():raise SignInCancelled('Sign-in cancelled')

    def status(self,text):self.commands.put(('status',text))
    def close(self):
        self.commands.put(('close',''))
        self.thread.join(timeout=2)


def begin(code,url):
    global _current
    close()
    _current=SignInProgress(code,url)
    _current.wait_for_ok()


def check_cancelled():
    if _current:_current.check_cancelled()


def status(text):
    if _current:_current.status(text)


def close():
    global _current
    if _current:_current.close();_current=None


def close_when_game_starts(is_running, timeout=60):
    if _current is None:return
    status('Sign-in verified. Starting Minecraft...')
    def wait():
        import time
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if is_running():close();return
            time.sleep(.25)
        close()
        from account_guard import alert
        alert('Sign-in completed, but Minecraft did not start. Please reopen Minecraft: Apple TV Edition.')
    threading.Thread(target=wait,daemon=True).start()
