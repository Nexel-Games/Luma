# Plugin trust

Discovery reads `.lplugin` JSON from `<EngineRoot>/Plugins` and `<ProjectRoot>/Plugins` only. It does not execute plugin code.

When `LUMA_MODULAR=ON`, `DllModuleBackend` loads `LumaEditor-<Module>.dll` only from those discovered plugin folders (`Binaries/Win64/<Config>/`), never from the process search path or the current directory. A native plugin that is linked or loaded has the same privileges as the engine: process memory, the file system the host can reach, every registry.

There is no sandbox and no signature check. Treat project `Plugins/` folders like you treat project `Source/`: only install plugins you would compile into the editor yourself.

`LumaEngine` still statically whole-archives Runtime modules; it does not LoadLibrary plugin code.
