The directive: This should be used at every step of the way. Whenever you are implementing a new feature, fixing a bug, adding a test, updating documentation, etc, you should consult this directive and make sure you are following it. Every detail is important.

If its missing in kernel, implement it. If it's missing in the user library, implement it (ulib). If it's missing in the tools, implement it. If it's missing in the build system, implement it. If it's missing in the documentation, implement it. If it's missing in the tests, implement it. 

NEVER USE THE xv6 MKFS. USE ONE OF THE HOST TOOLS. MK2FS WORKS GREAT AND IS FOR A FS WE ACTUALLY SUPPORT. 

We should avoid adding anything that should be core c library function to posix.c unless absolutely necessary, and instead add it to the actual c library/headers.

Whenever a document in docs/ is relevant to a change, update it. If there isn't a relevant document, add one. If there is a relevant document but it doesn't cover the change, update it to cover the change.

If you want to iterate over changes, building/making is recommended (make qemu or make qemu-nox). If you want to run a specific test, you can run it directly from the qemu console. If that doesn't work, bail out IMMEDIATELY and give me the command to run. Do not write a test and then spend hours trying to get it to run. Do not write a test and spend minutes trying to get it to run. Just give me the command and I'll run it for you. No harnesses, no automated boots, none of that. The session will be terminated. This is not negotiable.

When creating userland tools, make sure there is an entry in .gitignore for the binary and that it will
be cleaned up by make clean. Ensure you build a manpage for the tool and add it to the documentation. I don't want any binaries ending up in staged or in the repo or in the build output that aren't tracked by git or documented.

When writing filesystem code, use the vfs layer, the filesystem driver shouldn't have to handle anything with the device directly. If this isn't possible because of holes in the VFS layer,
we should plug those holes or adjust it accordingly. If this affects other parts of the codebase, 
we should find those and adjust accordingly. 

When implementing a new feature, we should try to implement it in a way that is as modular as possible,
so that it can be easily tested and maintained. We should also try to follow the existing code style and conventions, and ensure that the changes are in line with a unix or unix like system. Use NetBSD, OpenBSD, FreeBSD, Darwin, SerernityOS, etc as examples.

Build tests and stressors whenever possible. Also keep performance in mind. 

If you decided to iterate yourself, you can login to the system with root, password: root. Don't use "wait"/"sleep" etc for the login because they can fail. Use expect if you need to look for "login:" and "password" prompts. Qemu will not exit on its own, you'll need to killall it.

Feel free to turn on debug logging for the relevant subsystem to see the output in the qemu console. You can also add new debug logging if you need to see something specific.

The operating system gives the same serial output via uart, so if possible, watch stdout, strerr, etc and let me watch the output via the gui console. We have a lot of -nox targets too if you just want that output.

Whenever we add a debug line for a driver, program, function or feature, make sure to consult docs/debug-logging.md and find the best gate, or create a new one. Make sure to disable the debugging when 
the feature/etc is complete. 

The end goal is a unix-like operating system so we need to follow those conventions. The starting base
was very lightweight so there is a lot to add featurewise. Roadmap.md has the general roadmap.

When stubbing out driver support: consult documentation for the driver, but also look at the bsd/darwin/linux implementations. Be sure to fit it to auxv6 conventions and style, but also try to follow the general structure of the driver as much as possible to ensure we are implementing the same features and behavior. If a driver is small enough, we can implement it fully, but for larger drivers, we can stub out the basic structure and then fill in the details as we go.

Don't be afraid to add new built targets to makefile. If you need a configuration with a specific set of hardware, don't be afraid to add a new target with the correct qemu options and document it.


Other footguns (don't ignore these): 

- devman is our device node generator, it has nothing to do with man, the manual page viewer.
- you cannot run the user/_applications directly. They are compiled for a auxv6 system.
- make qemu is the command to run the system, sudo is required if the build host is macos
- make qemu-nox is the command to run the system without a graphical console, sudo is required if the build host is macos
- xv6fs is deprecated. don't spend any time on it. If you need to mount a filesystem, use the ext2 image.
- don't assume kernel code or headers are static targets, if you need to add a header, add it to include/ and update the relevant files. If you need to add a function, add it to the relevant file in kernel/ or ulib/ and update the relevant headers.
- don't use the mkfs that comes from original xv6, it is not compatible with the ext2 image. Use the tools/stage-ext2-root.sh script to create an ext2 image from a directory. You can use fakeroot or sudo to create the image with root ownership, which is required for some files to have the correct permissions. If you don't have fakeroot or sudo, you can run the script without them, but the files will be owned by the current user and may not have the correct permissions.
- if we determine that one misbehavior from a feature or program is a bug, but not the main bug, we should still fix it, but we should also make sure to document it in the relevant area.

