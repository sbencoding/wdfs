# Contributing
Thank you for considering contributing to the **wdfs** project.  

### Setting up the environment
Please follow the `Installation` and `Building` guide from the [readme](https://github.com/sbencoding/wdfs/blob/master/README.md) file.  

### Files
All the source code files are located under `src/`.  
All the build scripts and build related stuff is under `build/`.  
All the executable files and are under `bin/`.  

#### Source files
This is probably the most relevant section to contributors. Here you can find the files related to networking and the file system implementation, device enumeration etc...  
- **wdfs.cpp** - This is the file that implements the file system operations and creates a bridge between the network code and FUSE
- **wdfs.h** - This is the file where impelemented file system methods and methods of our FS code are defined. If you wish to implement a new file system operation, the definition would have to be added here.
- **wd_bridge.cpp** - This is the main file/entry point of the project. Handles commandline arugments, logs in to the device, and launches the file system.
- **device_locator.cpp** - This is the file that handles the listing of devices associated with the user. This is a separate main entry point and is not used by the other project files.
- **bridge.cpp** - This is the file where the networking code, https calls to the device and auth0 are implemented.
- **bridge.hpp** - This is where networking methods and some common data structures of bridge are defined.
- **json.hpp** - JSON library from [here](https://github.com/nlohmann/json) parses and stringifies json when needed by bridge code
- **Fuse.cpp, Fuse.h and Fuse-impl.h** - These files provide c++ support while still using FUSE, they're from the [Fusepp repo](https://github.com/jachappell/Fusepp)

The last 2 entries in the list are external libraries, please don't modify them if possible.

#### Build files
This is probably less important to contributors, but if the build directions have to be modified, then you can do that here.  
- **Makefile** - Replaces the legacy build scripts, builds the specified target using the `make` command
- **build.sh** - *Legacy script* used for building the filesystem code
- **run.sh** - Executes the filesystem binary
- **build-locator.sh** - *Legacy script* used for building the device locator code
- **run-locator.sh** - Executes the device locator binary

**run.sh** and **run-locator.sh** Fill credentials and device IDs from files that are excluded from the project using the `.gitignore` file.  
In order to make these 2 scripts work you have to manually create and fill these excluded files.
- **credentials.txt** - both run scripts read the credentials (username and password) from here. The format should be as follows: the first line is the username, the second line is the password
- **devid.txt** - `run.sh` fills the device ID argument from this file. You should put your device ID in the first line and nothing else

#### Bin files
This is the place where the binary files and build output gets stored.  
You will find the generated binaries here, and if for some reason you need to build a new binary file, please output it to here.  
- **wd_bridge** - The main program that provides the file system.
- **device_locator** - Program for listing devices associated with the given user.

### Code
Please use the conventions that are used the currently.  
Some of the key conventions that I used are:  
- snake_case_naming
- Using `for(a : b)` syntax if index is not required
- Comments have a space after them ex. `// this is a comment`
- Global variables are always stored at the top of the file

I'm open to contributors suggesting new sytle guidelines.  

### Crashes
##### Broken mount point
If you crash the FS program the mount point will become broken. And the next time you want to mount the FS it will fail.  
To fix this you can type `fusermount3 -u [mount_path]` to unmount the broken mount point, then re-execute your code.  

##### fusermount3 running after FS code exits
I ran into this problem while I was making the FS, that after closing the program, that was giving bad results to file system calls, but didn't crash, `fusermount3` and the executed command for example `ls` or `rm` would keep running.  
I also noticed a constant higher usage of the CPU.  
I was unable to kill the `fusermount3` process even with `kill -9 [PID]`.  
Even when I shutdown the system it would keep running and keep the machine from shutting down.  
The solution I found was to use `reboot -n -f` which forces the reboot (**-f**) and skips syncing file systems (**-n**).  

### Questions?
If you have any questions regarding to how the project works or you want to add something to this file that I've missed, then don't hesitate to open a new `Issue` for discussion.  
Thank you for reading the `CONTRIBUTING` guide, **happy coding**!
