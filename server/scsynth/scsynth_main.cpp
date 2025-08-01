/*
    SuperCollider real time audio synthesis system
    Copyright (c) 2002 James McCartney. All rights reserved.
    http://www.audiosynth.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include "SC_WorldOptions.h"
#include "SC_Version.hpp"
#include "SC_EventLoop.hpp"
#include "SC_ServerBootDelayWarning.h"
#include <cstring>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include "clz.h"
#include <stdexcept>
#ifdef _WIN32
#    include <winsock2.h>
#    include <vector>
#else
#    include <unistd.h> // for _POSIX_MEMLOCK
#    include <sys/wait.h>
#endif

#ifdef __COBALT__
#    include "XenomaiLock.h"
static XenomaiInitializer xenomaiInitializer;
#endif // __COBALT__

#ifdef _WIN32

// according to this page: http://www.mkssoftware.com/docs/man3/setlinebuf.3.asp
// setlinebuf is equivalent to the setvbuf call below.
inline int setlinebuf(FILE* stream) { return setvbuf(stream, (char*)0, _IONBF, 0); }

#endif

void Usage();
void Usage() {
    WorldOptions defaultOptions;

    scprintf("supercollider_synth  options:\n"
             "   -v print the supercollider version and exit\n"
             "   -u <udp-port-number> a port number 1-65535. Set to 0 to bind a free port.\n"
             "   -t <tcp-port-number> a port number 1-65535. Set to 0 to bind a free port.\n"
             "   -B <bind-to-address>\n"
             "          Bind the UDP or TCP socket to this address.\n"
             "          Default 127.0.0.1. Set to 0.0.0.0 to listen on all interfaces.\n"
             "   -c <number-of-control-bus-channels> (default %d)\n"
             "   -a <number-of-audio-bus-channels>   (default %d)\n"
             "   -i <number-of-input-bus-channels>   (default %d)\n"
             "   -o <number-of-output-bus-channels>  (default %d)\n"
             "   -z <block-size>                     (default %d)\n"
             "   -Z <hardware-buffer-size>           (default %d)\n"
             "   -S <hardware-sample-rate>           (default %d)\n"
             "   -b <number-of-sample-buffers>       (default %d)\n"
             "   -n <max-number-of-nodes>            (default %d)\n"
             "   -d <max-number-of-synth-defs>       (default %d)\n"
             "   -m <real-time-memory-size>          (default %d)\n"
             "   -w <number-of-wire-buffers>         (default %d)\n"
             "   -r <number-of-random-seeds>         (default %d)\n"
             "   -D <load synthdefs? 1 or 0>         (default %d)\n"
             "   -R <publish to Rendezvous? 1 or 0>  (default %d)\n"
             "   -l <max-logins>                     (default %d)\n"
             "          maximum number of named return addresses stored\n"
             "          also maximum number of tcp connections accepted\n"
             "   -p <session-password>\n"
             "          When using TCP, the session password must be the first command sent.\n"
             "          The default is no password.\n"
             "          UDP ports never require passwords, so for security use TCP.\n"
             "   -N <cmd-filename> <input-filename> <output-filename> <sample-rate> <header-format> <sample-format>\n"
#ifdef __APPLE__
             "   -s <safety-clip-threshold>          (default %d)\n"
             "          absolute amplitude value output signals will be clipped to.\n"
             "          Set to <= 0 or inf to completely disable output clipping.\n"
             "   -I <input-streams-enabled>\n"
             "   -O <output-streams-enabled>\n"
#endif
#ifdef SC_BELA
             "   -J <bela-analog-input-channels>\n"
             "   -K <bela-analog-output-channels>\n"
             "   -G <bela-digital-channels>\n"
             "   -Q <bela-headphone-level> (0dB max, -63.5dB min)\n"
             "   -X <bela-pga-gain-left>\n"
             "   -Y <bela-pga-gain-right>\n"
             "   -A <bela-speaker-mute>\n"
             "   -x <bela-dac-level>\n"
             "   -y <bela-adc-level>\n"
             "   -g <bela-multiplexer-channels>\n"
             "   -T <bela-pru-id>\n"
             "   -E <bela-oscilloscope-max-channels>\n"
#endif // SC_BELA
#if (_POSIX_MEMLOCK - 0) >= 200112L
             "   -L enable memory locking\n"
#endif
             "   -H <hardware-device-name>\n"
             "   -V <verbosity>\n"
             "          0 is normal behaviour.\n"
             "          -1 suppresses informational messages.\n"
             "          -2 suppresses informational and many error messages, as well as\n"
             "             messages from Poll.\n"
             "          The default is 0.\n"
#ifdef _WIN32
             "   -U <ugen-plugins-path>\n"
             "          A list of paths separated by `;`.\n"
#else
             "   -U <ugen-plugins-path>\n"
             "          A list of paths separated by `:`.\n"
#endif
             "          If specified, standard paths are NOT searched for plugins.\n"
             "   -P <restricted-path>    \n"
             "          if specified, prevents file-accessing OSC commands from\n"
             "          accessing files outside <restricted-path>.\n"
             "\nTo quit, send a 'quit' command via UDP or TCP, or press ctrl-C.\n\n",
             defaultOptions.mNumControlBusChannels, defaultOptions.mNumAudioBusChannels,
             defaultOptions.mNumInputBusChannels, defaultOptions.mNumOutputBusChannels, defaultOptions.mBufLength,
             defaultOptions.mPreferredHardwareBufferFrameSize, defaultOptions.mPreferredSampleRate,
             defaultOptions.mNumBuffers, defaultOptions.mMaxNodes, defaultOptions.mMaxGraphDefs,
             defaultOptions.mRealTimeMemorySize, defaultOptions.mMaxWireBufs, defaultOptions.mNumRGens,
             defaultOptions.mRendezvous, defaultOptions.mLoadGraphDefs, defaultOptions.mMaxLogins
#ifdef __APPLE__
             ,
             defaultOptions.mSafetyClipThreshold
#endif
    );
    exit(1);
}

#define checkNumArgs(n)                                                                                                \
    if (i + n > argc) {                                                                                                \
        if (n == 2)                                                                                                    \
            scprintf("ERROR: Argument expected after option %s\n", argv[j]);                                           \
        else                                                                                                           \
            scprintf("ERROR: More arguments expected after option %s\n", argv[j]);                                     \
        Usage();                                                                                                       \
    }                                                                                                                  \
    i += n;


int scsynth_main(int argc, char** argv) {
    ServerBootDelayWarningTimer bootDelayWarningTimer;
    bootDelayWarningTimer.start();

    setlinebuf(stdout);

    EventLoop::setup();

    int udpPortNum = -1;
    int tcpPortNum = -1;
    std::string bindTo("127.0.0.1");

    WorldOptions options;

#ifdef SC_BELA
    // defaults
    options.mBelaAnalogInputChannels = 0;
    options.mBelaAnalogOutputChannels = 0;
    options.mBelaDigitalChannels = 0;
    options.mBelaHeadphoneLevel = -6.;
    options.mBelaPgaGainLeft = 20;
    options.mBelaPgaGainRight = 20;
    options.mBelaSpeakerMuted = 0;
    options.mBelaAdcLevel = 0;
    options.mBelaDacLevel = 0;
    options.mBelaNumMuxChannels = 0;
    options.mBelaPru = 1;
    options.mBelaMaxScopeChannels = 0;
#endif // SC_BELA

    for (int i = 1; i < argc;) {
#if defined(SC_BELA)
#    define SC_EXTRA_OPTIONS "JKGQXYAxygTE"
#else // SC_BELA
#    define SC_EXTRA_OPTIONS ""
#endif // SC_BELA
        if (argv[i][0] != '-' || argv[i][1] == 0
            || strchr("utBaioczblndpmwZrCNSDIOsMHvVRUhPL" SC_EXTRA_OPTIONS, argv[i][1]) == nullptr) {
            scprintf("ERROR: Invalid option %s\n", argv[i]);
            Usage();
        }
        int j = i;
        switch (argv[j][1]) {
        case 'u':
            checkNumArgs(2);
            udpPortNum = atoi(argv[j + 1]);
            break;
        case 't':
            checkNumArgs(2);
            tcpPortNum = atoi(argv[j + 1]);
            break;
        case 'B':
            checkNumArgs(2);
            bindTo = argv[j + 1];
            break;
        case 'a':
            checkNumArgs(2);
            options.mNumAudioBusChannels = atoi(argv[j + 1]);
            break;
        case 'i':
            checkNumArgs(2);
            options.mNumInputBusChannels = atoi(argv[j + 1]);
            break;
        case 'o':
            checkNumArgs(2);
            options.mNumOutputBusChannels = atoi(argv[j + 1]);
            break;
        case 'c':
            checkNumArgs(2);
            options.mNumControlBusChannels = atoi(argv[j + 1]);
            break;
        case 'z':
            checkNumArgs(2);
            options.mBufLength = NEXTPOWEROFTWO(atoi(argv[j + 1]));
            break;
        case 'Z':
            checkNumArgs(2);
            options.mPreferredHardwareBufferFrameSize = NEXTPOWEROFTWO(atoi(argv[j + 1]));
            break;
        case 'b':
            checkNumArgs(2);
            options.mNumBuffers = NEXTPOWEROFTWO(atoi(argv[j + 1]));
            break;
        case 'l':
            checkNumArgs(2);
            options.mMaxLogins = NEXTPOWEROFTWO(atoi(argv[j + 1]));
            break;
        case 'n':
            checkNumArgs(2);
            options.mMaxNodes = NEXTPOWEROFTWO(atoi(argv[j + 1]));
            break;
        case 'd':
            checkNumArgs(2);
            options.mMaxGraphDefs = NEXTPOWEROFTWO(atoi(argv[j + 1]));
            break;
        case 'p':
            checkNumArgs(2);
            options.mPassword = argv[j + 1];
            break;
        case 'm':
            checkNumArgs(2);
            options.mRealTimeMemorySize = atoi(argv[j + 1]);
            break;
        case 'w':
            checkNumArgs(2);
            options.mMaxWireBufs = atoi(argv[j + 1]);
            break;
        case 'r':
            checkNumArgs(2);
            options.mNumRGens = atoi(argv[j + 1]);
            break;
        case 'S':
            checkNumArgs(2);
            options.mPreferredSampleRate = (uint32)atof(argv[j + 1]);
            break;
        case 'D':
            checkNumArgs(2);
            options.mLoadGraphDefs = atoi(argv[j + 1]);
            break;
        case 'N':
#ifdef NO_LIBSNDFILE
            scprintf("NRT mode not supported: scsynth compiled without libsndfile\n");
            exit(0);
#endif
            // -N cmd-filename input-filename output-filename sample-rate header-format sample-format
            checkNumArgs(7);
            options.mRealTime = false;
            options.mNonRealTimeCmdFilename = strcmp(argv[j + 1], "_") ? argv[j + 1] : nullptr;
            options.mNonRealTimeInputFilename = strcmp(argv[j + 2], "_") ? argv[j + 2] : nullptr;
            options.mNonRealTimeOutputFilename = argv[j + 3];
            options.mPreferredSampleRate = (uint32)atof(argv[j + 4]);
            options.mNonRealTimeOutputHeaderFormat = argv[j + 5];
            options.mNonRealTimeOutputSampleFormat = argv[j + 6];
            break;
#ifdef __APPLE__
        case 'I':
            checkNumArgs(2);
            options.mInputStreamsEnabled = argv[j + 1];
            break;
        case 'O':
            checkNumArgs(2);
            options.mOutputStreamsEnabled = argv[j + 1];
            break;
        case 'M':
#endif
        case 's':
            checkNumArgs(2);
            options.mSafetyClipThreshold = atof(argv[j + 1]);
            break;
        case 'H':
            checkNumArgs(2);
            options.mInDeviceName = argv[j + 1];
            if (i + 1 > argc || argv[j + 2][0] == '-') {
                options.mOutDeviceName = options.mInDeviceName;
            } else {
                // If there's a second argument then the user wants separate I/O devices
                options.mOutDeviceName = argv[j + 2];
                ++i;
            }
            break;
        case 'L':
            checkNumArgs(1);
#if (_POSIX_MEMLOCK - 0) >= 200112L
            options.mMemoryLocking = true;
#else
            options.mMemoryLocking = false;
#endif
            break;
#ifdef SC_BELA
        case 'J':
            checkNumArgs(2);
            options.mBelaAnalogInputChannels = atoi(argv[j + 1]);
            break;
        case 'K':
            checkNumArgs(2);
            options.mBelaAnalogOutputChannels = atoi(argv[j + 1]);
            break;
        case 'G':
            checkNumArgs(2);
            options.mBelaDigitalChannels = atoi(argv[j + 1]);
            break;
        case 'Q':
            checkNumArgs(2);
            options.mBelaHeadphoneLevel = atof(argv[j + 1]);
            break;
        case 'X':
            checkNumArgs(2);
            options.mBelaPgaGainLeft = atof(argv[j + 1]);
            break;
        case 'Y':
            checkNumArgs(2);
            options.mBelaPgaGainRight = atof(argv[j + 1]);
            break;
        case 'A':
            checkNumArgs(2);
            options.mBelaSpeakerMuted = atoi(argv[j + 1]) > 0;
            break;
        case 'x':
            checkNumArgs(2);
            options.mBelaDacLevel = atof(argv[j + 1]);
            break;
        case 'y':
            checkNumArgs(2);
            options.mBelaAdcLevel = atof(argv[j + 1]);
            break;
        case 'g':
            checkNumArgs(2);
            options.mBelaNumMuxChannels = atoi(argv[j + 1]);
            break;
        case 'T':
            checkNumArgs(2);
            options.mBelaPru = atoi(argv[j + 1]);
            break;
        case 'E':
            checkNumArgs(2);
            options.mBelaMaxScopeChannels = atoi(argv[j + 1]);
            break;
#endif // SC_BELA
        case 'V':
            checkNumArgs(2);
            options.mVerbosity = atoi(argv[j + 1]);
            break;
        case 'v':
            scprintf("scsynth %s (%s)\n", SC_VersionString().c_str(), SC_BuildString().c_str());
            exit(0);
            break;
        case 'R':
            checkNumArgs(2);
            options.mRendezvous = atoi(argv[j + 1]) > 0;
            break;
        case 'U':
            checkNumArgs(2);
            options.mUGensPluginPath = argv[j + 1];
            break;
        case 'P':
            checkNumArgs(2);
            options.mRestrictedPath = argv[j + 1];
            break;
        case 'C':
            checkNumArgs(2);
            break;
        case 'h':
        default:
            Usage();
        }
    }
    if (udpPortNum == -1 && tcpPortNum == -1 && options.mRealTime) {
        scprintf("ERROR: There must be a -u and/or a -t options, or -N for nonrealtime.\n");
        Usage();
    }
    if (options.mNumInputBusChannels + options.mNumOutputBusChannels > options.mNumAudioBusChannels) {
        scprintf("ERROR: number of audio bus channels < inputs + outputs.\n");
        Usage();
    }

    if (options.mRealTime) {
        int port = (udpPortNum > 0) ? udpPortNum : tcpPortNum;

        options.mSharedMemoryID = port;
    } else
        options.mSharedMemoryID = 0;

    struct World* world = World_New(&options);
    if (!world)
        return 1;

    if (!options.mRealTime) {
        // Server is ready!
        bootDelayWarningTimer.stop();
#ifdef NO_LIBSNDFILE
        return 1;
#else
        try {
            World_NonRealTimeSynthesis(world, &options);
            return 0;
        } catch (const std::exception& exc) {
            scprintf("NRT synthesis failed: %s\n", exc.what());
            World_Cleanup(world, true);
            return 1;
        }
#endif
    }

    if (udpPortNum >= 0) {
        if (!World_OpenUDP(world, bindTo.c_str(), udpPortNum)) {
            World_Cleanup(world, true);
            return 1;
        }
    }
    if (tcpPortNum >= 0) {
        if (!World_OpenTCP(world, bindTo.c_str(), tcpPortNum, options.mMaxLogins, 8)) {
            World_Cleanup(world, true);
            return 1;
        }
    }

    // Server is ready!
    bootDelayWarningTimer.stop();

    if (options.mVerbosity >= 0) {
#ifdef NDEBUG
        scprintf("SuperCollider 3 server ready.\n");
#else
        scprintf("SuperCollider 3 server ready (debug build).\n");
#endif
    }
    fflush(stdout);

    EventLoop::run([world]() { World_WaitForQuit(world, true); });

    return 0;
}

#ifdef _WIN32

int wmain(int argc, wchar_t** wargv) {
    // initialize winsock
    WSAData wsaData;
    int nCode;
    if ((nCode = WSAStartup(MAKEWORD(1, 1), &wsaData)) != 0) {
        scprintf("WSAStartup() failed with error code %d.\n", nCode);
        return 1;
    }

    // convert args to utf-8
    std::vector<char*> argv;
    for (int i = 0; i < argc; i++) {
        auto argSize = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        argv.push_back(new char[argSize]);
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], argSize, nullptr, nullptr);
    }

    // set codepage to UTF-8 and remember the old codepage
    auto oldCodePage = GetConsoleOutputCP();
    if (!SetConsoleOutputCP(65001))
        scprintf("WARNING: could not set codepage to UTF-8\n");

    // run main
    int result = scsynth_main(argv.size(), argv.data());

    // clean up winsock
    WSACleanup();
    // reset codepage from UTF-8
    SetConsoleOutputCP(oldCodePage);
    // clear vector with converted args
    for (auto* arg : argv)
        delete[] arg;

    return result;
}

#else

int main(int argc, char** argv) { return scsynth_main(argc, argv); };

#endif //_WIN32
