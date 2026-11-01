using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Text.Json;

namespace MinecraftAppleTVBuilder;

internal sealed record Request(string AppleTVTipa, string DesktopAppx, bool BuildOnly, string OriginalUserSid, string? OutputDirectory = null);

internal static class Program
{
    public static readonly string CacheRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MinecraftAppleTVBuilder");
    public static string CurrentSid => WindowsIdentity.GetCurrent().User?.Value ?? throw new InvalidOperationException("Cannot identify the Windows account.");
    public static bool IsAdministrator => new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);

    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        try
        {
            string? Argument(string name) { int i = Array.IndexOf(args, name); return i >= 0 && i + 1 < args.Length ? args[i + 1] : null; }
            if (Argument("--self-test") is { } report)
            {
                SelfTest(report);
                return 0;
            }
            if (Argument("--verify-build") is { } verification)
            {
                var request = new Request(Argument("--apple-tv") ?? throw new ArgumentException("Missing --apple-tv"), Argument("--desktop") ?? throw new ArgumentException("Missing --desktop"), true, CurrentSid, Argument("--output"));
                int code = VerifyBuildAsync(request, verification).GetAwaiter().GetResult();
                return code;
            }
            Request? resume = null;
            if (Argument("--resume") is { } resumeFile)
            {
                resume = JsonSerializer.Deserialize<Request>(File.ReadAllText(resumeFile)) ?? throw new InvalidDataException("Restart information is empty.");
                if (resume.OriginalUserSid != CurrentSid) throw new InvalidOperationException("Elevation used a different Windows account. Restart under the original account to avoid installing the game for the wrong person.");
            }
            Application.Run(new BuilderForm(resume));
            return 0;
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Minecraft Apple TV Builder", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    public static Stream Payload() => Assembly.GetExecutingAssembly().GetManifestResourceStream("BuilderPayload.zip") ?? throw new InvalidOperationException("The embedded builder payload is missing.");

    public static string ExtractPayload()
    {
        string destination = Path.Combine(CacheRoot, "App", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(destination);
        using var stream = Payload();
        using var zip = new ZipArchive(stream);
        string prefix = Path.GetFullPath(destination) + Path.DirectorySeparatorChar;
        foreach (var entry in zip.Entries)
        {
            if (string.IsNullOrEmpty(entry.Name)) continue;
            string target = Path.GetFullPath(Path.Combine(destination, entry.FullName));
            if (!target.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Invalid embedded payload path.");
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            entry.ExtractToFile(target, false);
        }
        return destination;
    }

    public static ProcessStartInfo WorkerStartInfo(string payload, Request request)
    {
        var info = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32", "WindowsPowerShell", "v1.0", "powershell.exe"))
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8, WorkingDirectory = payload
        };
        foreach (string arg in new[] { "-NoProfile", "-NonInteractive", "-STA", "-ExecutionPolicy", "Bypass", "-File", Path.Combine(payload, "Build-MinecraftAppleTV.ps1"), "-GuiWorker", "-AppleTVTipa", request.AppleTVTipa, "-DesktopAppx", request.DesktopAppx, "-OriginalUserSid", request.OriginalUserSid }) info.ArgumentList.Add(arg);
        // Avoid inheriting incompatible PowerShell 7 module paths in the 5.1 worker.
        info.Environment["PSModulePath"] = string.Join(Path.PathSeparator, new[] {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "WindowsPowerShell", "Modules"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "WindowsPowerShell", "Modules"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32", "WindowsPowerShell", "v1.0", "Modules") });
        if (request.BuildOnly) info.ArgumentList.Add("-BuildOnly");
        if (request.OutputDirectory is { Length: > 0 }) { info.ArgumentList.Add("-OutputDirectory"); info.ArgumentList.Add(request.OutputDirectory); }
        return info;
    }

    private static void SelfTest(string report)
    {
        using var stream = Payload(); using var zip = new ZipArchive(stream);
        foreach (string name in new[] { "Build-MinecraftAppleTV.ps1", "Builder.Core.ps1", "GuiBridge.ps1", "Build.json", "priconfig.xml" })
            if (zip.GetEntry(name) is null) throw new InvalidDataException("Missing payload: " + name);
        using var configStream = zip.GetEntry("Build.json")!.Open(); using var config = JsonDocument.Parse(configStream);
        int frameworks = 0;
        foreach (var dependency in config.RootElement.GetProperty("Frameworks").EnumerateArray())
        {
            string name = "Dependencies/" + dependency.GetProperty("File").GetString();
            using var bytes = (zip.GetEntry(name) ?? throw new InvalidDataException(name)).Open();
            string hash = Convert.ToHexString(SHA256.HashData(bytes));
            if (hash != dependency.GetProperty("SHA256").GetString()) throw new InvalidDataException("Framework integrity failure: " + name);
            frameworks++;
        }
        using var hashesStream = zip.GetEntry("PayloadHashes.json")!.Open();
        using var hashes = JsonDocument.Parse(hashesStream);
        foreach (var file in hashes.RootElement.EnumerateArray())
        {
            string name = file.GetProperty("path").GetString()!;
            using var data = (zip.GetEntry(name) ?? throw new InvalidDataException(name)).Open();
            if (!Convert.ToHexString(SHA256.HashData(data)).Equals(file.GetProperty("sha256").GetString(), StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Payload integrity failure: " + name);
        }
        string? coreLibrary = Process.GetCurrentProcess().Modules.Cast<ProcessModule>().FirstOrDefault(m => m.ModuleName.Equals("coreclr.dll", StringComparison.OrdinalIgnoreCase))?.FileName;
        File.WriteAllText(report, JsonSerializer.Serialize(new { status = "passed", architecture = RuntimeInformation.ProcessArchitecture.ToString(), runtime = RuntimeEnvironment.GetRuntimeDirectory(), framework = RuntimeInformation.FrameworkDescription, coreRuntimeLibrary = coreLibrary, embeddedFiles = zip.Entries.Count, verifiedFrameworks = frameworks, version = "1.0.0" }, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static async Task<int> VerifyBuildAsync(Request request, string report)
    {
        string payload = ExtractPayload(); var events = new List<string>();
        using var worker = Process.Start(WorkerStartInfo(payload, request)) ?? throw new InvalidOperationException("Could not start the builder.");
        Task<string> errors = worker.StandardError.ReadToEndAsync();
        while (await worker.StandardOutput.ReadLineAsync().ConfigureAwait(false) is { } line)
        {
            events.Add(line);
            if (line.StartsWith("@@MCGUI@@"))
            {
                using var doc = JsonDocument.Parse(line[9..]);
                if (doc.RootElement.GetProperty("type").GetString() == "question") { await worker.StandardInput.WriteLineAsync("n"); await worker.StandardInput.FlushAsync(); }
            }
        }
        await worker.WaitForExitAsync();
        File.WriteAllText(report, JsonSerializer.Serialize(new { exitCode = worker.ExitCode, events, stderr = await errors }, new JsonSerializerOptions { WriteIndented = true }));
        return worker.ExitCode;
    }
}

internal sealed class BuilderForm : Form
{
    private static readonly Color Background = Color.FromArgb(15, 23, 31), Card = Color.FromArgb(24, 37, 49), TextColor = Color.FromArgb(236, 244, 247), Muted = Color.FromArgb(161, 180, 193), Accent = Color.FromArgb(79, 222, 175);
    private readonly TextBox mobile = new(), desktop = new();
    private readonly CheckBox buildOnly = new() { Text = "Build APPX only (skip game installation)", AutoSize = true };
    private readonly RichTextBox log = new();
    private readonly Label status = new(), stage = new();
    private readonly ProgressBar progress = new();
    private readonly Button install = new(), exit = new(), openOutput = new();
    private readonly List<Control> inputs = new();
    private bool busy;
    private string? outputFolder;
    private string? resumedOutputDirectory;

    public BuilderForm(Request? resume)
    {
        Text = "Minecraft: Apple TV Edition Builder";
        BackColor = Background; ForeColor = TextColor; Font = new Font("Segoe UI", 10);
        AutoScaleMode = AutoScaleMode.Dpi; StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1060, 840); MinimumSize = new Size(920, 760);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 5, Padding = new Padding(26), BackColor = Background };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        foreach (float height in new[] { 158f, 308f, 90f }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 76));
        Controls.Add(layout);

        var banner = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, ColumnCount = 1, Margin = new Padding(0, 0, 0, 20), Padding = new Padding(24, 18, 24, 12), BackColor = Card };
        banner.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        banner.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); banner.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        banner.Controls.Add(new Label { Text = "MINECRAFT: APPLE TV EDITION BUILDER", Font = new Font("Segoe UI", 20, FontStyle.Bold), ForeColor = TextColor, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, Margin = new Padding(0, 0, 0, 10) });
        var github = new LinkLabel { Text = "github.com/thebreenis", LinkColor = Color.FromArgb(115, 197, 255), ActiveLinkColor = Accent, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoSize = false, AccessibleName = "TheBreenis on GitHub", Margin = new Padding(0) };
        github.LinkClicked += (_, _) => Process.Start(new ProcessStartInfo("https://github.com/thebreenis") { UseShellExecute = true });
        banner.Controls.Add(github); layout.Controls.Add(banner, 0, 0);

        var files = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 6, BackColor = Card, Padding = new Padding(20, 16, 20, 14), Margin = new Padding(0, 0, 0, 12) };
        files.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 160)); files.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); files.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 108));
        foreach (float height in new[] { 31f, 29f, 49f, 25f, 49f, 25f }) files.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
        var hint = new Label { Text = "Supply the Windows 1.1.5 APPX and decrypted Apple TV 1.1.5 TIPA.", ForeColor = TextColor, Font = new Font(Font, FontStyle.Bold), Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
        files.Controls.Add(hint, 0, 0); files.SetColumnSpan(hint, 3);
        var sourceNote = new Label { Text = "Your original files stay unchanged. Both input checksums are verified.", ForeColor = Muted, Font = new Font("Segoe UI", 9), Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
        files.Controls.Add(sourceNote, 0, 1); files.SetColumnSpan(sourceNote, 3);
        AddInput(files, 2, "01  Apple TV TIPA", mobile, "Select the decrypted Apple TV TIPA");
        var mobileName = new Label { Text = "Minecraft_1.1.5_decrypted.tipa", ForeColor = Muted, Font = new Font("Segoe UI", 9), Dock = DockStyle.Fill, Margin = new Padding(160, 0, 0, 0) };
        files.Controls.Add(mobileName, 0, 3); files.SetColumnSpan(mobileName, 3);
        AddInput(files, 4, "02  Desktop x64", desktop, "Select the matching desktop x64 APPX");
        var desktopName = new Label { Text = "Minecraft-1.1.5.0.Appx", ForeColor = Muted, Font = new Font("Segoe UI", 9), Dock = DockStyle.Fill, Margin = new Padding(160, 0, 0, 0) };
        files.Controls.Add(desktopName, 0, 5); files.SetColumnSpan(desktopName, 3);
        var inputArea = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Margin = new Padding(0) };
        inputArea.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); inputArea.RowStyles.Add(new RowStyle(SizeType.Absolute, 37));
        inputArea.Controls.Add(files, 0, 0);
        buildOnly.ForeColor = TextColor; buildOnly.Margin = new Padding(6, 2, 0, 8); buildOnly.CheckedChanged += (_, _) => install.Text = buildOnly.Checked ? "Build APPX" : "Build & Install";
        inputArea.Controls.Add(buildOnly, 0, 1); inputs.Add(buildOnly); layout.Controls.Add(inputArea, 0, 1);

        var progressPanel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Margin = new Padding(0, 0, 0, 10) };
        progressPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); progressPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 10)); progressPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        status.Text = "Ready when you are"; status.Font = new Font(Font, FontStyle.Bold); status.Dock = DockStyle.Fill;
        progress.Dock = DockStyle.Fill; progress.Style = ProgressBarStyle.Continuous;
        stage.Text = "Input packages stay unchanged. Build output is saved on this PC."; stage.ForeColor = Muted; stage.Dock = DockStyle.Fill; stage.Font = new Font("Segoe UI", 9); stage.Margin = new Padding(0, 10, 0, 0);
        progressPanel.Controls.Add(status); progressPanel.Controls.Add(progress); progressPanel.Controls.Add(stage); layout.Controls.Add(progressPanel, 0, 2);

        log.ReadOnly = true; log.BackColor = Color.FromArgb(10, 18, 25); log.ForeColor = Color.FromArgb(194, 214, 225); log.BorderStyle = BorderStyle.None; log.Font = new Font("Consolas", 9); log.Dock = DockStyle.Fill; log.WordWrap = true; log.ScrollBars = RichTextBoxScrollBars.Vertical; log.AccessibleName = "Build and installation log"; log.Margin = new Padding(0);
        layout.Controls.Add(log, 0, 3);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft, Padding = new Padding(0, 13, 0, 0), Margin = new Padding(0), WrapContents = false };
        StyleButton(install, "Build & Install", true); StyleButton(exit, "Exit", false); StyleButton(openOutput, "Open output folder", false); openOutput.Width = 178; openOutput.Enabled = false;
        actions.Controls.Add(install); actions.Controls.Add(exit); actions.Controls.Add(openOutput); layout.Controls.Add(actions, 0, 4);
        install.Click += async (_, _) => await RunAsync(); exit.Click += (_, _) => Close();
        openOutput.Click += (_, _) => OpenOutputFolder();
        FormClosing += (_, e) => { if (busy) { e.Cancel = true; MessageBox.Show(this, "A build or installation is still running. Please let it finish before closing.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information); } };
        if (resume is not null) { mobile.Text = resume.AppleTVTipa; desktop.Text = resume.DesktopAppx; buildOnly.Checked = resume.BuildOnly; resumedOutputDirectory = resume.OutputDirectory; }
        AppendLog(Program.IsAdministrator ? "Administrator mode. Review your inputs and click Build & Install to start." : "Choose both original packages. If administrator access is needed, you will be asked before restarting.");
    }

    private void AddInput(TableLayoutPanel table, int row, string label, TextBox box, string dialogTitle)
    {
        table.Controls.Add(new Label { Text = label, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, ForeColor = TextColor }, 0, row);
        box.Dock = DockStyle.Fill; box.BackColor = Background; box.ForeColor = TextColor; box.BorderStyle = BorderStyle.FixedSingle; box.Margin = new Padding(0, 6, 9, 5); box.AccessibleName = label + " package path"; box.AllowDrop = true;
        box.DragEnter += (_, e) => e.Effect = !busy && e.Data?.GetDataPresent(DataFormats.FileDrop) == true ? DragDropEffects.Copy : DragDropEffects.None;
        box.DragDrop += (_, e) => { if (!busy && e.Data?.GetData(DataFormats.FileDrop) is string[] paths && paths.Length > 0) box.Text = paths[0]; };
        var browse = new Button(); StyleButton(browse, "Browse…", false); browse.Dock = DockStyle.Fill; browse.Margin = new Padding(0, 3, 0, 4); browse.AccessibleName = dialogTitle;
        browse.Click += (_, _) => { using var dialog = new OpenFileDialog { Title = dialogTitle, Filter = box == mobile ? "Apple TV packages (*.tipa)|*.tipa" : "Windows packages (*.appx)|*.appx", CheckFileExists = true }; if (dialog.ShowDialog(this) == DialogResult.OK) box.Text = dialog.FileName; };
        table.Controls.Add(box, 1, row); table.Controls.Add(browse, 2, row); inputs.Add(box); inputs.Add(browse);
    }

    private static void StyleButton(Button button, string text, bool primary)
    {
        button.UseMnemonic = false;
        button.Text = text; button.Width = 148; button.Height = 38; button.FlatStyle = FlatStyle.Flat; button.FlatAppearance.BorderSize = primary ? 0 : 1; button.FlatAppearance.BorderColor = Color.FromArgb(62, 85, 101); button.BackColor = primary ? Accent : Card; button.ForeColor = primary ? Color.FromArgb(10, 38, 30) : TextColor; button.Font = new Font("Segoe UI", 10, primary ? FontStyle.Bold : FontStyle.Regular); button.Cursor = Cursors.Hand; button.Margin = new Padding(10, 0, 0, 0);
    }
    private void AppendLog(string message) { if (string.IsNullOrWhiteSpace(message)) return; log.AppendText(message.Trim() + Environment.NewLine); log.SelectionStart = log.TextLength; log.ScrollToCaret(); }
    private void SetBusy(bool value) { busy = value; foreach (var control in inputs) control.Enabled = !value; install.Enabled = exit.Enabled = !value; openOutput.Enabled = !value && outputFolder is not null; }

    private async Task RunAsync()
    {
        if (!File.Exists(mobile.Text.Trim()) || !File.Exists(desktop.Text.Trim())) { MessageBox.Show(this, "Select the original APPX and decrypted TIPA first.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information); return; }
        string? destination = resumedOutputDirectory;
        if (destination is null || Directory.Exists(destination))
        {
            using var chooser = new FolderBrowserDialog
            {
                Description = "Choose where to save the APPX and certificate. A new Minecraft Apple TV folder will be created here.",
                UseDescriptionForTitle = true, ShowNewFolderButton = true,
                SelectedPath = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)
            };
            if (chooser.ShowDialog(this) != DialogResult.OK) return;
            destination = Path.Combine(chooser.SelectedPath, "Minecraft-AppleTV-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N")[..6]);
        }
        resumedOutputDirectory = null;
        var request = new Request(Path.GetFullPath(mobile.Text.Trim()), Path.GetFullPath(desktop.Text.Trim()), buildOnly.Checked, Program.CurrentSid, Path.GetFullPath(destination));
        SetBusy(true); outputFolder = null; progress.Style = ProgressBarStyle.Marquee; status.Text = "Checking requirements…"; stage.Text = "Preparing the embedded builder";
        string? elevationReason = null;
        try
        {
            string payload = await Task.Run(Program.ExtractPayload);
            using var worker = Process.Start(Program.WorkerStartInfo(payload, request)) ?? throw new InvalidOperationException("Could not start Windows PowerShell.");
            Task stderr = ReadErrorsAsync(worker);
            while (await worker.StandardOutput.ReadLineAsync() is { } line)
            {
                if (!line.StartsWith("@@MCGUI@@")) { AppendLog(line); continue; }
                using var doc = JsonDocument.Parse(line[9..]); var data = doc.RootElement;
                string? type = data.GetProperty("type").GetString();
                string message = data.TryGetProperty("message", out var m) ? m.GetString() ?? "" : "";
                switch (type)
                {
                    case "log": AppendLog(message); break;
                    case "progress":
                        int percent = data.GetProperty("percent").GetInt32();
                        progress.Style = percent >= 20 && percent < 85 ? ProgressBarStyle.Marquee : ProgressBarStyle.Continuous;
                        if (progress.Style == ProgressBarStyle.Continuous) progress.Value = Math.Clamp(percent, 0, 100);
                        status.Text = message; stage.Text = percent >= 20 && percent < 85 ? "Working… packaging more than 4,500 files can take several minutes." : "Progress is reported by build stage.";
                        break;
                    case "question":
                        bool yes = MessageBox.Show(this, message, Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) == DialogResult.Yes;
                        await worker.StandardInput.WriteLineAsync(yes ? "y" : "n"); await worker.StandardInput.FlushAsync();
                        break;
                    case "elevation": elevationReason = message; break;
                    case "result": outputFolder = Path.GetFullPath(data.GetProperty("folder").GetString() ?? throw new InvalidDataException("No output folder was returned.")); break;
                }
            }
            await worker.WaitForExitAsync(); await stderr;
            if (elevationReason is not null)
            {
                status.Text = "Administrator access required"; stage.Text = "This run stopped before the privileged operation.";
                SetBusy(false);
                if (MessageBox.Show(this, "Administrator access is needed for " + elevationReason + ".\n\nRelaunch in elevated mode? The new window starts over; review the selected files and click Install again.", Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) == DialogResult.Yes) RestartElevated(request);
                return;
            }
            if (worker.ExitCode != 0 || outputFolder is null) throw new InvalidOperationException("The builder stopped before completion. See the log for the reason and manual setup instructions.");
            progress.Style = ProgressBarStyle.Continuous; progress.Value = 100; status.Text = request.BuildOnly ? "Your signed APPX is ready" : "Minecraft: Apple TV Edition is installed"; stage.Text = outputFolder;
            AppendLog("Completed. Output: " + outputFolder);
            if (request.BuildOnly) OpenOutputFolder();
        }
        catch (Exception ex) { progress.Style = ProgressBarStyle.Continuous; progress.Value = 0; status.Text = "Stopped"; stage.Text = "Your original input files were not changed."; AppendLog(ex.Message); MessageBox.Show(this, ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        finally { progress.Style = ProgressBarStyle.Continuous; SetBusy(false); }
    }

    private void OpenOutputFolder()
    {
        if (outputFolder is null) return;
        try
        {
            string folder = new DirectoryInfo(outputFolder).FullName;
            if (!Directory.Exists(folder)) { AppendLog("The output folder is no longer available: " + folder); return; }
            var explorer = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe")) { UseShellExecute = true };
            explorer.ArgumentList.Add(folder);
            Process.Start(explorer);
        }
        catch (Exception ex) { AppendLog("Build completed, but File Explorer could not be opened: " + ex.Message); }
    }
    private async Task ReadErrorsAsync(Process worker) { while (await worker.StandardError.ReadLineAsync() is { } line) AppendLog(line); }
    private void RestartElevated(Request request)
    {
        try
        {
            string directory = Path.Combine(Program.CacheRoot, "Requests"); Directory.CreateDirectory(directory);
            string file = Path.Combine(directory, Guid.NewGuid().ToString("N") + ".json"); File.WriteAllText(file, JsonSerializer.Serialize(request));
            var start = new ProcessStartInfo(Environment.ProcessPath ?? throw new InvalidOperationException("Cannot locate the executable.")) { UseShellExecute = true, Verb = "runas", Arguments = "--resume \"" + file + "\"" };
            Process.Start(start); Close();
        }
        catch (Exception ex) { AppendLog("Elevated restart was not completed: " + ex.Message); }
    }
}
