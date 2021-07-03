# wdfs
**wdfs** is a fuse filesystem written in c++, based on my previous project [WD-Bridge](https://github.com/sbencoding/WD-Bridge)  
The previous project required the user to run a separate prompt and interact with the device from the prompt.  
It also lacked features such as relative path support for most of the operations and could only upload/download files to the current working directory.  
To get past these limitations I've decided to create a file system, where you can use already existing, robust tools such as `cd`, `cp`, `cat`, `tail` and much more.  

### Installation
**wdfs** has 2 dependencies that need to be installed on your system before building the binaries.  
1. **CURL/libcurl**  
`libcurl` is required to perform https operations, which are used to communicated with the my cloud home device.
It acts as a bridge between the web interface and the local system.  
`libcurl` is installed by default on many systems, but if you don't have it you can use your distro's package manager to do it.  
**Examples**:  
`sudo apt-get install curl libcurl4 libcurl4-openssl-dev` for Debian based systems.  
`pacman -S curl libcurl-openssl` for Arch based systems. etc...  
2. **FUSE/libfuse**  
`libfuse` is required to expose a file system to the kernel through `FUSE` and implement the methods of a file system.  
Version 3.x of `libfuse` is required.  
**Installation**:  
`sudo apt-get install fuse3 libfuse3-3 libfuse3-dev` for Debian based systems.  
`pacman -S fuse-common fuse3` for Arch based systems. etc...  

### Building
If you've installed all the required dependencies listed above, you can build the project.  
First `git clone` the project and then `cd` into the created directory.  
After this go into the `build` folder and execute the `make fs` command to build the file system.  
The `Makefile` uses g++ as it's compiler by default, 32 bit machines are automatically detected using the `getconf LONG_BIT` command and the required build flags are added accordingly.  

After the build successfully finishes the binary is generated at the path `bin/wd_bridge`.  
*note*: The compiler may throw some warnings, these will be fixed in the future. Don't worry, the binary is usable regardless.  
*note*: The `*.o` files are generated by `make`. These files make the recompilation of the project faster. If you're not planning to rebuild the project often, or these files are simply just annoying to you, you can always execute `make clean` in the `build` folder to remove these files.  

### Running
After you've built the project you can run it. To run the project go into the `bin` folder and execute the `wd_bridge` file.  
The binary requires 4 parameters to be executed.  
1. **username** - The username/email address you use for logging in to your device [here](https://home.mycloud.com/#)
2. **password** - The password you use with your username to log in to your device
3. **A mount point** - An already existing folder where `wd_bridge` can mount your remote device.
4. **Your devcie ID** - The ID of your WD My Cloud Home device.  
Example: `device-local-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`.  
If you don't know this don't worry read the [Device ID](#Device-ID) section of this readme.  

After specifying the correct arguments `wd_bridge` will run and mount the root of your device to the given mount point.  
You can now start using `ls` and `cat` etc. to explore the file system and the files.  
*note*: If you're not on the same network as the device, `wd_bride` will automatically try to use the *port forwarding* connection method.  

### Device ID
`wd_bridge` has to know the ID of the device to connect to, in order to mount it.  
To figure out the ID of the device you wish to mount, I've written a separate program called `device_locator`.  
##### Building
Go into the `build` folder and execute the `make locator` command, this should build the `device_locator` program.  

After the build finishes, you can execute the `bin/device_locator`.
*note*: The `*.o` files are generated by `make`. These files make the recompilation of the project faster. If you're not planning to rebuild the project often, or these files are simply just annoying to you, you can always execute `make clean` in the `build` folder to remove these files.  
##### Running
`device_locator` requires 2 arguments to run.  
1. **username** - The username/email address you use for logging in to your device [here](https://home.mycloud.com/#)
2. **password** - The password you use with your username to log in to your device

After specifying the correct arguments `device_locator` will list the devices associated with your account in the format of:  
```
[index] My device name (my device id)
```
*note*: Only My Cloud Home has been tested for support, other non-My cloud home devices that are associated with your account might not work as intended.  
After you have the list and decided which device to use, simply copy the id of the device and put `device-local-` in front of it to get an ID like: `device-local-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`  

### Future
Current filesystem operations supported:
 * `read`
 * `write` (overwriting at arbitary position is terribly slow, because the lack of server-side support for it)
 * `open`
 * `release`
 * `readdir`
 * `getattr` (currently timestamps are fake)
 * `mkdir`
 * `unlink`
 * `rmdir`
 * `create`
 * `rename`
 * `truncate`
 * `utimens` (access time is not supported on the server side and the server doesn't support nanosecond precision)

There are also features that I don't know how to interface with a file system like `Sharing files` and other device specific non-file system features.  
Constantly improve performance where it's possible without compromising on up-to-date state.  
**TODO**:
- [x] Look into supporting non-local device communication
- [ ] Get correct timestamps when listing files
- [ ] Investigate long running connections (i.e. re-login if session times out, switch from remote to local connection and vice-versa automatically when needed)
- [ ] Investigate options to automatically mount with system startup using `fstab`

### Contribution
The project accepts contributions in the form of `Pull Requests` and `Issues`.  
Testing the file system would be awesome, so if you could test and report: crashes, unexpected behaviour, missing features in `Issues`, I'd be grateful.  
If you want to contribute code improvements or build stuff to the project you can open a `Pull Request`, where I try to respond to you as fast as I can.  
