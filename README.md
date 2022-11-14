# Windows 32 Windowed Simple Server Applications

This application is written as a challenge to create a simple HTTP server. I thought it was a good idea to
brush up my Windows API skill. 

![Program Runs on Windows 98 SE](/images/ss.png "Screenshots running on Windows 98 SE")

The server runs with full Graphical User Interface because nobody cares about "Command Line" back in the 90's. 
This is a naive implementation where it can only serve one client at a time.

This program is built using Visual Studio 97, Visual Studio 6, and Visual Studio 2005. It can runs on Windows 95 OSR 2 
with [Winsock2 Update](https://winworldpc.com/product/windows-95/patches) to Windows 11.

The resulting binary is ~30KB for full fledged server. This includes the pretty icon. Without icon, it'll take maybe
~15KB of EXE.

Workstation to build this program

- Pentium 166 MMX 
- 16 MB of RAM
- Windows NT 4.0 Service Pack 6 and Windows 98 SE.

Other than making a point that programmer nowadays needs beefy computer just to create a 'command line' hello world with 
1000x the power of my old workstation, this program also showcases: 

- How to program Windows GUI in pure C.
- How to programmatically build UI using code like real programmer. No
  designers, or bloated "UI toolkit" involved. 
- How to use thread synchronisation in Windows with `CreateThread`.
- How to send message from one thread to another using `PostMessage` and
  `SendNotifyMessage`. 
- A single binary that can run on every Windows from the 90's to 2022 and
  probably still runs 20+ years later. Microsoft is 'da king' of API
  compatibility. 
- To prove that Windows API is the most successful API, because you can even
  run this using Wine. 

## Enhancements 

I plan to add enhancements

- [ ] Multiple client using thread.
- [ ] Windows 9x vs NT detection.
- [ ] HTTP 1.0 compliant.
- [ ] TLS 1.2 with mbedTLS.
- [ ] JSON parser.
- [ ] Thread pool with semaphore for Win9x. 
- [ ] Thread pool with IO Completion Ports for Windows NT.
- [ ] Better UI design.
- [ ] Ability to be run as a Windows Service.
- [ ] Ability to dynamically dispatch function in DLL as HTTP Handler. CGI be damned.
- [ ] Portability on the socket part with Linux and macOS. 

When will those be done? I don't know. If you feel like it, please contribute.
The only hard requirement is this program should be able to run on
Windows 95 and NT 4.0. So most likely, if you want fancy features, you'd need to do that by using `LoadLibrary`.


# Copyright and License 

All code in this repository is licensed under BSD 2-clause license.

```
BSD 2-Clause License

Copyright (c) 2022 Didiet Noor

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
