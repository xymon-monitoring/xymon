# ###################################################################################
# 
# Xymon client for Windows
#
# This is a client implementation for Windows systems that support the
# Powershell scripting language.
#
# Copyright (C) 2010 Henrik Storner <henrik@hswn.dk>
# Copyright (C) 2010 David Baldwin
# Copyright (c) 2014-2017 Accenture (zak.beck@accenture.com)
#
#   Contributions to this project were made by Accenture starting from June 2014.
#   For a list of modifications, please see the SVN change log.
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#  
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#  
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# ###################################################################################

# -----------------------------------------------------------------------------------
# User configurable settings
# -----------------------------------------------------------------------------------

$xymonservers = @( "xymonhost" )    # List your Xymon servers here
# $clientname  = "winxptest"    # Define this to override the default client hostname

$xymonsvcname = "XymonPSClient"
$xymondir = split-path -parent $MyInvocation.MyCommand.Definition

# -----------------------------------------------------------------------------------

$Version = '2.27'
$XymonClientVersion = "${Id}: xymonclient.ps1  $Version 2017-12-11 zak.beck@accenture.com"
# detect if we're running as 64 or 32 bit
$XymonRegKey = $(if([System.IntPtr]::Size -eq 8) { "HKLM:\SOFTWARE\Wow6432Node\XymonPSClient" } else { "HKLM:\SOFTWARE\XymonPSClient" })
$XymonClientCfg = join-path $xymondir 'xymonclient_config.xml'
$ServiceChecks = @{}

$UnixEpochOriginUTC = New-Object DateTime 1970,1,1,0,0,0,([DateTimeKind]::Utc)

Add-Type -AssemblyName System.Web

#region dotNETHelperTypes
function AddHelperTypes
{
$getprocessowner = @'
// see: http://www.codeproject.com/Articles/14828/How-To-Get-Process-Owner-ID-and-Current-User-SID
// adapted slightly and bugs fixed
using System;
using System.Runtime.InteropServices;
using System.Diagnostics;

public class GetProcessOwner
{

    public const int TOKEN_QUERY = 0X00000008;

    const int ERROR_NO_MORE_ITEMS = 259;

    enum TOKEN_INFORMATION_CLASS                           
    {
        TokenUser = 1,
        TokenGroups,
        TokenPrivileges,
        TokenOwner,
        TokenPrimaryGroup,
        TokenDefaultDacl,
        TokenSource,
        TokenType,
        TokenImpersonationLevel,
        TokenStatistics,
        TokenRestrictedSids,
        TokenSessionId
    }

    [StructLayout(LayoutKind.Sequential)]
    struct TOKEN_USER
    {
        public _SID_AND_ATTRIBUTES User;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct _SID_AND_ATTRIBUTES
    {
        public IntPtr Sid;
        public int Attributes;
    }

    [DllImport("advapi32")]
    static extern bool OpenProcessToken(
        IntPtr ProcessHandle, // handle to process
        int DesiredAccess, // desired access to process
        ref IntPtr TokenHandle // handle to open access token
    );

    [DllImport("kernel32")]
    static extern IntPtr GetCurrentProcess();

    [DllImport("advapi32", CharSet = CharSet.Auto)]
    static extern bool GetTokenInformation(
        IntPtr hToken,
        TOKEN_INFORMATION_CLASS tokenInfoClass,
        IntPtr TokenInformation,
        int tokeInfoLength,
        ref int reqLength
    );

    [DllImport("kernel32")]
    static extern bool CloseHandle(IntPtr handle);

    [DllImport("advapi32", CharSet = CharSet.Auto)]
    static extern bool ConvertSidToStringSid(
        IntPtr pSID,
        [In, Out, MarshalAs(UnmanagedType.LPTStr)] ref string pStringSid
    );

    [DllImport("advapi32", CharSet = CharSet.Auto)]
    static extern bool ConvertStringSidToSid(
        [In, MarshalAs(UnmanagedType.LPTStr)] string pStringSid,
        ref IntPtr pSID
    );

    /// <span class="code-SummaryComment"><summary></span>
    /// Collect User Info
    /// <span class="code-SummaryComment"></summary></span>
    /// <span class="code-SummaryComment"><param name="pToken">Process Handle</param></span>
    public static bool DumpUserInfo(IntPtr pToken, out IntPtr SID)
    {
        int Access = TOKEN_QUERY;
        IntPtr procToken = IntPtr.Zero;
        bool ret = false;
        SID = IntPtr.Zero;
        try
        {
            if (OpenProcessToken(pToken, Access, ref procToken))
            {
                ret = ProcessTokenToSid(procToken, out SID);
                CloseHandle(procToken);
            }
            return ret;
        }
        catch //(Exception err)
        {
            return false;
        }
    }

    private static bool ProcessTokenToSid(IntPtr token, out IntPtr SID)
    {
        TOKEN_USER tokUser;
        const int bufLength = 256;            
        IntPtr tu = Marshal.AllocHGlobal(bufLength);
        bool ret = false;
        SID = IntPtr.Zero;
        try
        {
            int cb = bufLength;
            ret = GetTokenInformation(token, 
                    TOKEN_INFORMATION_CLASS.TokenUser, tu, cb, ref cb);
            if (ret)
            {
                tokUser = (TOKEN_USER)Marshal.PtrToStructure(tu, typeof(TOKEN_USER));
                SID = tokUser.User.Sid;
            }
            return ret;
        }
        catch //(Exception err)
        {
            return false;
        }
        finally
        {
            Marshal.FreeHGlobal(tu);
        }
    }

    public static string GetProcessOwnerByPId(int PID)
    {                                                                  
        IntPtr _SID = IntPtr.Zero;                                       
        string SID = String.Empty;                                             
        try                                                             
        {                                                                
            Process process = Process.GetProcessById(PID);
            if (DumpUserInfo(process.Handle, out _SID))
            {                                                                    
                ConvertSidToStringSid(_SID, ref SID);
            }

            // convert SID to username
            string account = new System.Security.Principal.SecurityIdentifier(SID).Translate(typeof(System.Security.Principal.NTAccount)).ToString();

            return account;                                          
        }                                                                           
        catch
        {                                                                           
            return "Unknown";
        }
    }
}
'@

$type = Add-Type $getprocessowner

$getprocesscmdline = @'
    // ZB adapted from ProcessHacker (http://processhacker.sf.net)
    using System;
    using System.Diagnostics;
    using System.Runtime.InteropServices;

    public class ProcessInformation
    {
        [DllImport("ntdll.dll")]
        internal static extern int NtQueryInformationProcess(
            [In] IntPtr ProcessHandle,
            [In] int ProcessInformationClass,
            [Out] out ProcessBasicInformation ProcessInformation,
            [In] int ProcessInformationLength,
            [Out] [Optional] out int ReturnLength
            );

        [DllImport("ntdll.dll")]
        public static extern int NtReadVirtualMemory(
            [In] IntPtr processHandle,
            [In] [Optional] IntPtr baseAddress,
            [In] IntPtr buffer,
            [In] IntPtr bufferSize,
            [Out] [Optional] out IntPtr returnLength
            );

        private const int FLS_MAXIMUM_AVAILABLE = 128;
        
        //Win32
        //private const int GDI_HANDLE_BUFFER_SIZE = 34;
        //Win64
        private const int GDI_HANDLE_BUFFER_SIZE = 60;

        private enum PebOffset
        {
            CommandLine,
            CurrentDirectoryPath,
            DesktopName,
            DllPath,
            ImagePathName,
            RuntimeData,
            ShellInfo,
            WindowTitle
        }

        [Flags]
        public enum RtlUserProcessFlags : uint
        {
            ParamsNormalized = 0x00000001,
            ProfileUser = 0x00000002,
            ProfileKernel = 0x00000004,
            ProfileServer = 0x00000008,
            Reserve1Mb = 0x00000020,
            Reserve16Mb = 0x00000040,
            CaseSensitive = 0x00000080,
            DisableHeapDecommit = 0x00000100,
            DllRedirectionLocal = 0x00001000,
            AppManifestPresent = 0x00002000,
            ImageKeyMissing = 0x00004000,
            OptInProcess = 0x00020000
        }

        [Flags]
        public enum StartupFlags : uint
        {
            UseShowWindow = 0x1,
            UseSize = 0x2,
            UsePosition = 0x4,
            UseCountChars = 0x8,
            UseFillAttribute = 0x10,
            RunFullScreen = 0x20,
            ForceOnFeedback = 0x40,
            ForceOffFeedback = 0x80,
            UseStdHandles = 0x100,
            UseHotkey = 0x200
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct UnicodeString
        {
            public ushort Length;
            public ushort MaximumLength;
            public IntPtr Buffer;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ListEntry
        {
            public IntPtr Flink;
            public IntPtr Blink;
        }

        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct Peb
        {
            public static readonly int ImageSubsystemOffset =
                Marshal.OffsetOf(typeof(Peb), "ImageSubsystem").ToInt32();
            public static readonly int LdrOffset =
                Marshal.OffsetOf(typeof(Peb), "Ldr").ToInt32();
            public static readonly int ProcessHeapOffset =
                Marshal.OffsetOf(typeof(Peb), "ProcessHeap").ToInt32();
            public static readonly int ProcessParametersOffset =
                Marshal.OffsetOf(typeof(Peb), "ProcessParameters").ToInt32();

            [MarshalAs(UnmanagedType.I1)]
            public bool InheritedAddressSpace;
            [MarshalAs(UnmanagedType.I1)]
            public bool ReadImageFileExecOptions;
            [MarshalAs(UnmanagedType.I1)]
            public bool BeingDebugged;
            [MarshalAs(UnmanagedType.I1)]
            public bool BitField;
            public IntPtr Mutant;

            public IntPtr ImageBaseAddress;
            public IntPtr Ldr; // PebLdrData*
            public IntPtr ProcessParameters; // RtlUserProcessParameters*
            public IntPtr SubSystemData;
            public IntPtr ProcessHeap;
            public IntPtr FastPebLock;
            public IntPtr AtlThunkSListPtr;
            public IntPtr SparePrt2;
            public int EnvironmentUpdateCount;
            public IntPtr KernelCallbackTable;
            public int SystemReserved;
            public int SpareUlong;
            public IntPtr FreeList;
            public int TlsExpansionCounter;
            public IntPtr TlsBitmap;
            public unsafe fixed int TlsBitmapBits[2];
            public IntPtr ReadOnlySharedMemoryBase;
            public IntPtr ReadOnlySharedMemoryHeap;
            public IntPtr ReadOnlyStaticServerData;
            public IntPtr AnsiCodePageData;
            public IntPtr OemCodePageData;
            public IntPtr UnicodeCaseTableData;

            public int NumberOfProcessors;
            public int NtGlobalFlag;

            public long CriticalSectionTimeout;
            public IntPtr HeapSegmentReserve;
            public IntPtr HeapSegmentCommit;
            public IntPtr HeapDeCommitTotalFreeThreshold;
            public IntPtr HeapDeCommitFreeBlockThreshold;

            public int NumberOfHeaps;
            public int MaximumNumberOfHeaps;
            public IntPtr ProcessHeaps;

            public IntPtr GdiSharedHandleTable;
            public IntPtr ProcessStarterHelper;
            public int GdiDCAttributeList;
            public IntPtr LoaderLock;

            public int OSMajorVersion;
            public int OSMinorVersion;
            public short OSBuildNumber;
            public short OSCSDVersion;
            public int OSPlatformId;
            public int ImageSubsystem;
            public int ImageSubsystemMajorVersion;
            public int ImageSubsystemMinorVersion;
            public IntPtr ImageProcessAffinityMask;
            public unsafe fixed byte GdiHandleBuffer[GDI_HANDLE_BUFFER_SIZE];
            public IntPtr PostProcessInitRoutine;

            public IntPtr TlsExpansionBitmap;
            public unsafe fixed int TlsExpansionBitmapBits[32];

            public int SessionId;

            public long AppCompatFlags;
            public long AppCompatFlagsUser;
            public IntPtr pShimData;
            public IntPtr AppCompatInfo;

            public UnicodeString CSDVersion;

            public IntPtr ActivationContextData;
            public IntPtr ProcessAssemblyStorageMap;
            public IntPtr SystemDefaultActivationContextData;
            public IntPtr SystemAssemblyStorageMap;

            public IntPtr MinimumStackCommit;

            public IntPtr FlsCallback;
            public ListEntry FlsListHead;
            public IntPtr FlsBitmap;
            public unsafe fixed int FlsBitmapBits[FLS_MAXIMUM_AVAILABLE / (sizeof(int) * 8)];
            public int FlsHighIndex;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct RtlUserProcessParameters
        {
            public static readonly int CurrentDirectoryOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "CurrentDirectory").ToInt32();
            public static readonly int DllPathOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "DllPath").ToInt32();
            public static readonly int ImagePathNameOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "ImagePathName").ToInt32();
            public static readonly int CommandLineOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "CommandLine").ToInt32();
            public static readonly int EnvironmentOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "Environment").ToInt32();
            public static readonly int WindowTitleOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "WindowTitle").ToInt32();
            public static readonly int DesktopInfoOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "DesktopInfo").ToInt32();
            public static readonly int ShellInfoOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "ShellInfo").ToInt32();
            public static readonly int RuntimeDataOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "RuntimeData").ToInt32();
            public static readonly int CurrentDirectoriesOffset =
                Marshal.OffsetOf(typeof(RtlUserProcessParameters), "CurrentDirectories").ToInt32();

            public struct CurDir
            {
                public UnicodeString DosPath;
                public IntPtr Handle;
            }

            public struct RtlDriveLetterCurDir
            {
                public ushort Flags;
                public ushort Length;
                public uint TimeStamp;
                public IntPtr DosPath;
            }

            public int MaximumLength;
            public int Length;

            public RtlUserProcessFlags Flags;
            public int DebugFlags;

            public IntPtr ConsoleHandle;
            public int ConsoleFlags;
            public IntPtr StandardInput;
            public IntPtr StandardOutput;
            public IntPtr StandardError;

            public CurDir CurrentDirectory;
            public UnicodeString DllPath;
            public UnicodeString ImagePathName;
            public UnicodeString CommandLine;
            public IntPtr Environment;

            public int StartingX;
            public int StartingY;
            public int CountX;
            public int CountY;
            public int CountCharsX;
            public int CountCharsY;
            public int FillAttribute;

            public StartupFlags WindowFlags;
            public int ShowWindowFlags;
            public UnicodeString WindowTitle;
            public UnicodeString DesktopInfo;
            public UnicodeString ShellInfo;
            public UnicodeString RuntimeData;

            public RtlDriveLetterCurDir CurrentDirectories;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ProcessBasicInformation
        {
            public int ExitStatus;
            public IntPtr PebBaseAddress;
            public IntPtr AffinityMask;
            public int BasePriority;
            public IntPtr UniqueProcessId;
            public IntPtr InheritedFromUniqueProcessId;
        }

        private static string GetProcessCommandLine(IntPtr handle)
        {
            ProcessBasicInformation pbi;

            int returnLength;
            int status = NtQueryInformationProcess(handle, 0, out pbi, Marshal.SizeOf(typeof(ProcessBasicInformation)), out returnLength);

            if (status != 0) throw new InvalidOperationException(string.Format("Exception: status = {0}, expecting 0", status));

            string result = GetPebString(PebOffset.CommandLine, pbi.PebBaseAddress, handle);

            return result;
        }

        private static string GetProcessImagePath(IntPtr handle)
        {
            ProcessBasicInformation pbi;

            int returnLength;
            int status = NtQueryInformationProcess(handle, 0, out pbi, Marshal.SizeOf(typeof(ProcessBasicInformation)), out returnLength);

            if (status != 0) throw new InvalidOperationException(string.Format("Exception: status = {0}, expecting 0", status));

            string result = GetPebString(PebOffset.ImagePathName, pbi.PebBaseAddress, handle);

            return result;
        }

        private static IntPtr IncrementPtr(IntPtr ptr, int value)
        {
            return IntPtr.Size == sizeof(Int32) ? new IntPtr(ptr.ToInt32() + value) : new IntPtr(ptr.ToInt64() + value);
        }

        private static unsafe string GetPebString(PebOffset offset, IntPtr pebBaseAddress, IntPtr handle)
        {
            byte* buffer = stackalloc byte[IntPtr.Size];

            ReadMemory(IncrementPtr(pebBaseAddress, Peb.ProcessParametersOffset), buffer, IntPtr.Size, handle);

            IntPtr processParameters = *(IntPtr*)buffer;
            int realOffset = GetPebOffset(offset);

            UnicodeString pebStr;
            ReadMemory(IncrementPtr(processParameters, realOffset), &pebStr, Marshal.SizeOf(typeof(UnicodeString)), handle);

            string str = System.Text.Encoding.Unicode.GetString(ReadMemory(pebStr.Buffer, pebStr.Length, handle), 0, pebStr.Length);

            return str;
        }

        private static int GetPebOffset(PebOffset offset)
        {
            switch (offset)
            {
                case PebOffset.CommandLine:
                    return RtlUserProcessParameters.CommandLineOffset;
                case PebOffset.CurrentDirectoryPath:
                    return RtlUserProcessParameters.CurrentDirectoryOffset;
                case PebOffset.DesktopName:
                    return RtlUserProcessParameters.DesktopInfoOffset;
                case PebOffset.DllPath:
                    return RtlUserProcessParameters.DllPathOffset;
                case PebOffset.ImagePathName:
                    return RtlUserProcessParameters.ImagePathNameOffset;
                case PebOffset.RuntimeData:
                    return RtlUserProcessParameters.RuntimeDataOffset;
                case PebOffset.ShellInfo:
                    return RtlUserProcessParameters.ShellInfoOffset;
                case PebOffset.WindowTitle:
                    return RtlUserProcessParameters.WindowTitleOffset;
                default:
                    throw new ArgumentException("offset");
            }
        }

        private static byte[] ReadMemory(IntPtr baseAddress, int length, IntPtr handle)
        {
            byte[] buffer = new byte[length];

            ReadMemory(baseAddress, buffer, length, handle);

            return buffer;
        }

        private static unsafe int ReadMemory(IntPtr baseAddress, byte[] buffer, int length, IntPtr handle)
        {
            fixed (byte* bufferPtr = buffer) return ReadMemory(baseAddress, bufferPtr, length, handle);
        }

        private static unsafe int ReadMemory(IntPtr baseAddress, void* buffer, int length, IntPtr handle)
        {
            return ReadMemory(baseAddress, new IntPtr(buffer), length, handle);
        }

        private static int ReadMemory(IntPtr baseAddress, IntPtr buffer, int length, IntPtr handle)
        {
            int status;
            IntPtr retLengthIntPtr;

            if ((status = NtReadVirtualMemory(handle, baseAddress, buffer, new IntPtr(length), out retLengthIntPtr)) > 0)
            {
                throw new InvalidOperationException(string.Format("Exception: status = {0}, expecting 0", status));
            }
            return retLengthIntPtr.ToInt32();
        }

        public static string GetCommandLineByProcessId(int PID)
        {
            string commandLine = "";
            try
            {
                Process process = Process.GetProcessById(PID);
                commandLine = GetProcessCommandLine(process.Handle);
                commandLine = commandLine.Replace((char)0, ' ');
            }
            catch
            {
            }
            return commandLine;
        }
    }
'@

$cp = new-object System.CodeDom.Compiler.CompilerParameters
$cp.CompilerOptions = "/unsafe"
$dummy = $cp.ReferencedAssemblies.Add('System.dll')

$type = Add-Type -TypeDefinition $getprocesscmdline -CompilerParameters $cp

$volumeinfo = @'
    using System;
    using System.Collections;
    using System.Runtime.InteropServices;
    using System.Text;
    using Microsoft.Win32.SafeHandles;

    public class VolumeInfo
    {
        [DllImport("kernel32.dll")]
        public static extern DriveType GetDriveType([MarshalAs(UnmanagedType.LPStr)] string lpRootPathName);

        [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetDiskFreeSpaceEx(string lpDirectoryName,
            out ulong lpFreeBytesAvailable,
            out ulong lpTotalNumberOfBytes,
            out ulong lpTotalNumberOfFreeBytes);

        [DllImport("Kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private extern static bool GetVolumeInformation(
            string RootPathName,
            StringBuilder VolumeNameBuffer,
            int VolumeNameSize,
            out uint VolumeSerialNumber,
            out uint MaximumComponentLength,
            out uint FileSystemFlags, // FileSystemFeature
            StringBuilder FileSystemNameBuffer,
            int nFileSystemNameSize);

        [DllImport("kernel32.dll", SetLastError=true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetVolumePathNamesForVolumeNameW(
            [MarshalAs(UnmanagedType.LPWStr)]
            string lpszVolumeName,
            [MarshalAs(UnmanagedType.LPWStr)]
            string lpszVolumePathNames,
            uint cchBuferLength,
            ref UInt32 lpcchReturnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern FindVolumeSafeHandle FindFirstVolume([Out] StringBuilder lpszVolumeName, uint cchBufferLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FindNextVolume(FindVolumeSafeHandle hFindVolume, [Out] StringBuilder lpszVolumeName, uint cchBufferLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FindVolumeClose(IntPtr hFindVolume);

        private static readonly ulong KB = 1024;

        public enum DriveType : uint
        {
            Unknown = 0,    //DRIVE_UNKNOWN
            Error = 1,        //DRIVE_NO_ROOT_DIR
            Removable = 2,    //DRIVE_REMOVABLE
            Fixed = 3,        //DRIVE_FIXED
            Remote = 4,        //DRIVE_REMOTE
            CDROM = 5,        //DRIVE_CDROM
            RAMDisk = 6        //DRIVE_RAMDISK
        }

        private class FindVolumeSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
        {
            private FindVolumeSafeHandle()
            : base(true)
            {
            }

            public FindVolumeSafeHandle(IntPtr preexistingHandle, bool ownsHandle)
            : base(ownsHandle)
            {
                SetHandle(preexistingHandle);
            }

            protected override bool ReleaseHandle()
            {
                return FindVolumeClose(handle);
            }
        }

        public class Volume
        {            
            public string VolumeGUID;            
            public string FileSys;
            public DriveType DriveType;
            public uint DriveTypeId;

            public string MountPoint;
            public string FileSystemName;
            public string VolumeName;
            
            public ulong TotalBytes;
            public ulong FreeBytes;
            public ulong UsedBytes;
            public int UsedPercent;

            public ulong TotalBytesKB;
            public ulong FreeBytesKB;
            public ulong UsedBytesKB;

            public uint SerialNumber;
        }

        private static void GetVolumeDetails(string drive, Volume v)
        {
            ulong FreeBytesToCallerDummy;
            if (GetDiskFreeSpaceEx(drive, out FreeBytesToCallerDummy, out v.TotalBytes, out v.FreeBytes))
            {
                StringBuilder volname = new StringBuilder(261);
                StringBuilder fsname = new StringBuilder(261);
                uint flagsDummy, maxlenDummy;
                GetVolumeInformation(drive, volname, volname.Capacity, 
                    out v.SerialNumber, out maxlenDummy, out flagsDummy, fsname, fsname.Capacity);
                v.FileSystemName = fsname.ToString();
                v.VolumeName = volname.ToString();

                if (v.TotalBytes > 0)
                {
                    double used = ((double)(v.TotalBytes - v.FreeBytes) / (double)v.TotalBytes);
                    v.UsedPercent = (int)Math.Round(used * 100.0);
                }

                v.UsedBytes = v.TotalBytes - v.FreeBytes;
                v.TotalBytesKB = v.TotalBytes / KB;
                v.FreeBytesKB = v.FreeBytes / KB;
                v.UsedBytesKB = v.UsedBytes / KB;
            }
        }

        private static void GetVolumeMountPoints(string volumeDeviceName, ArrayList volumes)
        {
            string buffer = "";
            uint lpcchReturnLength = 0;
            GetVolumePathNamesForVolumeNameW(volumeDeviceName, buffer, (uint)buffer.Length, ref lpcchReturnLength);
            if (lpcchReturnLength == 0)
            {
                return;
            }

            buffer = new string(new char[lpcchReturnLength]);

            if (!GetVolumePathNamesForVolumeNameW(volumeDeviceName, buffer, lpcchReturnLength, ref lpcchReturnLength))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());                
            }

            string[] mounts = buffer.Split('\0');
            if (buffer.Length > 1)
            {
                foreach (string mount in mounts)
                {
                    if (mount.Length > 0)
                    {
                        Volume v = new Volume();
                        v.VolumeGUID = volumeDeviceName;
                        v.MountPoint = mount;
                        v.DriveType = GetDriveType(mount);                        
                        v.DriveTypeId = (uint)v.DriveType;
                        if (mount[0] >= 'A' && mount[0] <= 'Z')
                        {
                            v.FileSys = mount[0].ToString();
                        }
                        if (mount.Length > 3)
                        {
                            // per BBWin, replace spaces with underscore in mountpoint name
                            v.FileSys = mount.Substring(3, mount.LastIndexOf('\\') - 3).Replace(' ', '_');                            
                        }
                        GetVolumeDetails(mount, v);
                        volumes.Add(v);
                    }
                }
            }
            else
            {
                // unmounted volume - only add details once
                Volume v = new Volume();
                v.VolumeGUID = volumeDeviceName;
                v.MountPoint = "";
                v.DriveType = GetDriveType(volumeDeviceName);                
                v.DriveTypeId = 99; // special value for unmounted
                v.FileSys = "unmounted";

                GetVolumeDetails(volumeDeviceName, v);
                volumes.Add(v);
            }
        }

        public static Volume[] GetVolumes()
        {
            const uint bufferLength = 1024;
            StringBuilder volume = new StringBuilder((int)bufferLength, (int)bufferLength);
            ArrayList ret = new ArrayList();

            using (FindVolumeSafeHandle volumeHandle = FindFirstVolume(volume, bufferLength))
            {
                if (volumeHandle.IsInvalid)
                {
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                }

                do
                {
                    GetVolumeMountPoints(volume.ToString(), ret);
                } while (FindNextVolume(volumeHandle, volume, bufferLength));

                return (Volume[])ret.ToArray(typeof(Volume));
            }
        }
    }
'@
$type = Add-Type $volumeinfo

}
#endregion 

function SetIfNot($obj,$key,$value)
{
    if($obj.$key -eq $null) { $obj | Add-Member -MemberType noteproperty -Name $key -Value $value }
}

function XymonConfig($startedWithArgs)
{
    if (Test-Path $XymonClientCfg)
    {
        XymonInitXML $startedWithArgs
        $script:XymonCfgLocation = "XML: $XymonClientCfg"
    }
    else
    {
        XymonInitRegistry
        $script:XymonCfgLocation = "Registry"
    }
    XymonInit
}
#'
function XymonInitXML($startedWithArgs)
{
    $xmlconfig = [xml](Get-Content $XymonClientCfg)
    $script:XymonSettings = $xmlconfig.XymonSettings

    # if serverhttppassword is populated and not encrypted, encrypt it
    # only if we were started without arguments - so don't do it for
    # service installation mode
    if ($startedWithArgs -eq $false -and
        $xmlconfig.XymonSettings.serverHttpPassword -ne '' -and
        $xmlconfig.XymonSettings.serverHttpPassword -notlike '{SecureString}*')
    {
        WriteLog 'Attempting to encrypt password in config file'
        try
        {
            $securePass = ConvertTo-SecureString -AsPlainText -Force $xmlconfig.XymonSettings.serverHttpPassword
            $encryptedPass = ConvertFrom-SecureString -SecureString $securePass
            $xmlSecPass = "{SecureString}$($encryptedPass)"
            $xmlconfig.XymonSettings.serverHttpPassword = $xmlSecPass
            $xmlconfig.Save($XymonClientCfg)
        }
        catch
        {
            WriteLog "Exception encrypting config file password: $_"
        }
    }
}

function XymonInitRegistry
{
    $script:XymonSettings = Get-ItemProperty -ErrorAction:SilentlyContinue $XymonRegKey
}

function XymonInit
{
    if($script:XymonSettings -eq $null) {
        $script:XymonSettings = New-Object Object
    } 

    $servers = $script:XymonSettings.servers
    SetIfNot $script:XymonSettings serversList $servers
    if ($script:XymonSettings.servers -match " ") 
    {
        $script:XymonSettings.serversList = $script:XymonSettings.servers.Split(" ")
    }
    if ($script:XymonSettings.serversList -eq $null)
    {
        $script:XymonSettings.serversList = $xymonservers
    }

    SetIfNot $script:XymonSettings serverUrl ''
    SetIfNot $script:XymonSettings serverHttpUsername ''
    SetIfNot $script:XymonSettings serverHttpPassword ''
    SetIfNot $script:XymonSettings serverHttpTimeoutMs 100000

    $wanteddisks = $script:XymonSettings.wanteddisks
    SetIfNot $script:XymonSettings wanteddisksList $wanteddisks
    if ($script:XymonSettings.wanteddisks -match " ") 
    {
        $script:XymonSettings.wanteddisksList = $script:XymonSettings.wanteddisks.Split(" ")
    }
    if ($script:XymonSettings.wanteddisksList -eq $null)
    {
        $script:XymonSettings.wanteddisksList = @( 3 ) # 3=Local disks, 4=Network shares, 2=USB, 5=CD
    }

    # Params for default clientname
    SetIfNot $script:XymonSettings clientfqdn 1 # 0 = unqualified, 1 = fully-qualified
    SetIfNot $script:XymonSettings clientlower 1 # 0 = unqualified, 1 = fully-qualified
    
    if ($script:XymonSettings.clientname -eq $null -or $script:XymonSettings.clientname -eq "") { # set name based on rules
        $ipProperties = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties()
        $clname  = $ipProperties.HostName
        if ($script:XymonSettings.clientfqdn -eq 1 -and ($ipProperties.DomainName -ne $null)) { 
            $clname += "." + $ipProperties.DomainName
        }
        if ($script:XymonSettings.clientlower -eq 1) { $clname = $clname.ToLower() }
        SetIfNot $script:XymonSettings clientname $clname
        $script:clientname = $clname
    }
    else
    {
        $script:clientname = $script:XymonSettings.clientname
    }

    # Params for various client options
    SetIfNot $script:XymonSettings clientbbwinmembug 1 # 0 = report correctly, 1 = page and virtual switched
    SetIfNot $script:XymonSettings clientremotecfgexec 0 # 0 = don't run remote config, 1 = run remote config
    SetIfNot $script:XymonSettings clientconfigfile "$env:TEMP\xymonconfig.cfg" # path for saved client-local.cfg section from server
    SetIfNot $script:XymonSettings clientlogfile "$env:TEMP\xymonclient.log" # path for logfile
    SetIfNot $script:XymonSettings clientsoftware "powershell" # powershell / bbwin
    SetIfNot $script:XymonSettings clientclass "powershell" # 'class' value (default powershell)
    SetIfNot $script:XymonSettings loopinterval 300 # seconds to repeat client reporting loop
    SetIfNot $script:XymonSettings maxlogage 60 # minutes age for event log reporting
    SetIfNot $script:XymonSettings MaxEvents 5000 # maximum number of events per event log
    SetIfNot $script:XymonSettings slowscanrate 72 # repeats of main loop before collecting slowly changing information again
    SetIfNot $script:XymonSettings reportevt 1 # scan eventlog and report (can be very slow)
    SetIfNot $script:XymonSettings EnableWin32_Product 0 # 0 = do not use Win32_product, 1 = do
                        # see http://support.microsoft.com/kb/974524 for reasons why Win32_Product is not recommended!
    SetIfNot $script:XymonSettings EnableWin32_QuickFixEngineering 0 # 0 = do not use Win32_QuickFixEngineering, 1 = do
    SetIfNot $script:XymonSettings EnableWMISections 0 # 0 = do not produce [WMI: sections (OS, BIOS, Processor, Memory, Disk), 1 = do
    SetIfNot $script:XymonSettings ClientProcessPriority 'Normal' # possible values Normal, Idle, High, RealTime, BelowNormal, AboveNormal

    $clientlogpath = Split-Path -Parent $script:XymonSettings.clientlogfile
    SetIfNot $script:XymonSettings clientlogpath $clientlogpath

    SetIfNot $script:XymonSettings clientlogretain 0

    SetIfNot $script:XymonSettings XymonAcceptUTF8 0 # messages sent to Xymon 0 = convert to ASCII, 1 = convert to UTF8
    SetIfNot $script:XymonSettings GetProcessInfoCommandLine 1 # get process command line 1 = yes, 0 = no
    SetIfNot $script:XymonSettings GetProcessInfoOwner 1 # get process owner 1 = yes, 0 = no

    $extscript = Join-Path $xymondir 'ext'
    $extdata = Join-Path $xymondir 'tmp'
    $localdata = Join-Path $xymondir 'local'
    SetIfNot $script:XymonSettings externalscriptlocation $extscript
    SetIfNot $script:XymonSettings externaldatalocation $extdata
    SetIfNot $script:XymonSettings localdatalocation $localdata
    SetIfNot $script:XymonSettings servergiflocation '/xymon/gifs/'
    $script:clientlocalcfg = ""
    $script:logfilepos = @{}
    $script:externals = @{}
    $script:diskpartData = ''
    $script:LastTransmissionMethod = 'Unknown'

    $script:HaveCmd = @{}
    foreach($cmd in "query","qwinsta") {
        $script:HaveCmd.$cmd = (get-command -ErrorAction:SilentlyContinue $cmd) -ne $null
    }

    @("cpuinfo","totalload","numcpus","numcores","numvcpus","osinfo","svcs","procs","disks",`
    "netifs","svcprocs","localdatetime","uptime","usercount",`
    "XymonProcsCpu","XymonProcsCpuTStart","XymonProcsCpuElapsed") `
    | %{ if (get-variable -erroraction SilentlyContinue $_) { Remove-Variable $_ }}
    
}

function XymonProcsCPUUtilisation
{
    # XymonProcsCpu is a table with 6 elements:
    #   0 = process object
    #   1 = last tick value
    #   2 = ticks used since last poll
    #   3 = activeflag
    #   4 = command line
    #   5 = owner

    # ZB - got a feeling XymonProcsCpuElapsed should be multiplied by number of cores
    if ((get-variable -erroraction SilentlyContinue "XymonProcsCpu") -eq $null) {
        $script:XymonProcsCpu = @{ 0 = ( $null, 0, 0, $false) }
        $script:XymonProcsCpuTStart = (Get-Date).ticks
        $script:XymonProcsCpuElapsed = 0
    }
    else {
        $script:XymonProcsCpuElapsed = (Get-Date).ticks - $script:XymonProcsCpuTStart
        $script:XymonProcsCpuTStart = (Get-Date).Ticks
    }
    $script:XymonProcsCpuElapsed *= $script:numcores
    
    foreach ($p in $script:procs) {
        # store the process name in XymonProcsCpu
        # and if $p.name differs but id matches, need to pick up new command line etc and zero counters
        # - this covers the case where a process id is reused
        $thisp = $script:XymonProcsCpu[$p.Id]
        if ($p.Id -ne 0 -and ($thisp -eq $null -or $thisp[0].Name -ne $p.Name))
        {
            # either we have not seen this process before ($thisp -eq $null)
            # OR
            # the name of the process for ID x does not equal the cached process name
            if ($thisp -eq $null)
            {
                WriteLog "New process $($p.Id) detected: $($p.Name)"
            }
            else
            {
                WriteLog "Process $($p.Id) appears to have changed from $($thisp[0].Name) to $($p.Name)"
            }

            $cmdline = ''
            $owner = ''
            if ($script:XymonSettings.GetProcessInfoCommandLine -eq 1)
            {
                $cmdline = [ProcessInformation]::GetCommandLineByProcessId($p.Id)
            }
            if ($script:XymonSettings.GetProcessInfoOwner -eq 1)
            {
                $owner = [GetProcessOwner]::GetProcessOwnerByPId($p.Id)
            }
            if ($owner.length -gt 32) { $owner = $owner.substring(0, 32) }

            # New process - create an entry in the curprocs table
            # We use static values here, because some get-process entries have null values
            # for the tick-count (The "SYSTEM" and "Idle" processes).
            $script:XymonProcsCpu[$p.Id] = @($null, 0, 0, $false, $cmdline, $owner)
            $thisp = $script:XymonProcsCpu[$p.Id]
        }

        $thisp[3] = $true
        $thisp[2] = $p.TotalProcessorTime.Ticks - $thisp[1]
        $thisp[1] = $p.TotalProcessorTime.Ticks
        $thisp[0] = $p
    }
}

function UserSessionCount
{
    if ($HaveCmd.qwinsta)
    {
        $script:usersessions = qwinsta /counter
        ($script:usersessions -match ' Active ').Length
    }
    else
    {
        $q = get-wmiobject win32_logonsession | %{ $_.logonid}
        $service = Get-WmiObject -ComputerName $server -Class Win32_Service -Filter "Name='$xymonsvc'"
        $s = 0
        get-wmiobject win32_session | ?{ 2,10 -eq $_.LogonType} | ?{$q -eq $_.logonid} | %{
            $z = $_.logonid
            get-wmiobject win32_sessionprocess | ?{ $_.Antecedent -like "*LogonId=`"$z`"*" } | %{
                if($_.Dependent -match "Handle=`"(\d+)`"") {
                    get-wmiobject win32_process -filter "processid='$($matches[1])'" }
            } | select -first 1 | %{ $s++ }
        }
        $s
    }
}

function XymonCollectInfo
{
    WriteLog "Executing XymonCollectInfo"

    CleanXymonProcsCpu
    WriteLog "XymonCollectInfo: Process info"
    $script:procs = Get-Process | Sort-Object -Property Id
    WriteLog "XymonCollectInfo: calling XymonProcsCPUUtilisation"
    XymonProcsCPUUtilisation

    WriteLog "XymonCollectInfo: CPU info (WMI)"
    $script:cpuinfo = @(Get-WmiObject -Class Win32_Processor)
    #$script:totalload = 0
    $script:numcpus  = $cpuinfo.Count
    $script:numcores = 0
    $script:numvcpus = 0
    foreach ($cpu in $cpuinfo) { 
        #$script:totalload += $cpu.LoadPercentage
        $script:numcores += $cpu.NumberOfCores
        $script:numvcpus += $cpu.NumberOfLogicalProcessors
    }
    #$script:totalload /= $numcpus

    WriteLog "Found $($script:numcpus) CPUs, total of $($script:numcores) cores"

    WriteLog "XymonCollectInfo: OS info (including memory) (WMI)"
    $script:osinfo = Get-WmiObject -Class Win32_OperatingSystem
    WriteLog "XymonCollectInfo: Service info (WMI)"
    $script:svcs = Get-WmiObject -Class Win32_Service | Sort-Object -Property Name
    WriteLog "XymonCollectInfo: Disk info"
    $mydisks = @()
    try
    {
        $volumes = [VolumeInfo]::GetVolumes()
        foreach ($disktype in $script:XymonSettings.wanteddisksList) { 
            $mydisks += @( ($volumes | where { $_.DriveTypeId -eq $disktype } ))
        }
    }
    catch
    {
        $volumes = @()
        WriteLog "Error getting volume information: $_"
    }
    $script:disks = $mydisks | Sort-Object FileSys

    WriteLog "XymonCollectInfo: Building table of service processes (uses WMI data)"
    $script:svcprocs = @{([int]-1) = ""}
    foreach ($s in $svcs) {
        if ($s.State -eq "Running") {
            if ($svcprocs[([int]$s.ProcessId)] -eq $null) {
                $script:svcprocs += @{ ([int]$s.ProcessId) = $s.Name }
            }
            else {
                $script:svcprocs[([int]$s.ProcessId)] += ("/" + $s.Name)
            }
        }
    }

    WriteLog "XymonCollectInfo: Date processing (uses WMI data)"
    $script:localdatetime = $osinfo.ConvertToDateTime($osinfo.LocalDateTime)
    $script:uptime = $localdatetime - $osinfo.ConvertToDateTime($osinfo.LastBootUpTime)
    
    WriteLog "XymonCollectInfo: Adding CPU usage etc to main process data"
    XymonProcesses

    WriteLog "XymonCollectInfo: calling UserSessionCount"
    $script:usercount = UserSessionCount

    WriteLog "XymonCollectInfo finished"
}

function WMIProp($class)
{
    $wmidata = Get-WmiObject -Class $class
    $props = ($wmidata | Get-Member -MemberType Property | Sort-Object -Property Name | where { $_.Name -notlike "__*" })
    foreach ($p in $props) {
        $p.Name + " : " + $wmidata.($p.Name)
    }
}

function UnixDate([System.DateTime] $t)
{
    $t.ToString("ddd dd MMM HH:mm:ss yyyy")
}

function epochTimeUtc([System.DateTime] $t)
{
    [int64]($t.ToUniversalTime() - $UnixEpochOriginUTC).TotalSeconds
}

function filesize($file,$clsize=4KB)
{
    return [math]::floor((($_.Length -1)/$clsize + 1) * $clsize/1KB)
}

function du([string]$dir,[int]$clsize=0)
{
    if($clsize -eq 0) {
        $drive = "{0}:" -f [string](get-item $dir | %{ $_.psdrive })
        $clsize = [int](Get-WmiObject win32_Volume | ? { $_.DriveLetter -eq $drive }).BlockSize
        if($clsize -eq 0 -or $clsize -eq $null) { $clsize = 4096 } # default in case not found
    }
    $sum = 0
    $dulist = ""
    get-childitem $dir -Force | % {
        if( $_.Attributes -like "*Directory*" ) {
           $dulist += du ("{0}\{1}" -f [string]$dir,$_.Name) $clsize | out-string
           $sum += $dulist.Split("`n")[-2].Split("`t")[0] # get size for subdir
        } else { 
           $sum += filesize $_ $clsize
        }
    }
    "$dulist$sum`t$dir"
}

function XymonPrintProcess($pobj, $name, $pct)
{
    $pcpu = (("{0:F1}" -f $pct) + "`%").PadRight(8)
    $ppid = ([string]($pobj.Id)).PadRight(9)
    
    if ($name.length -gt 30) { $name = $name.substring(0, 30) }
    $pname = $name.PadRight(32)

    $pprio = ([string]$pobj.BasePriority).PadRight(5)
    $ptime = (([string]($pobj.TotalProcessorTime)).Split(".")[0]).PadRight(9)
    $pmem = ([string]($pobj.WorkingSet64 / 1KB)) + "k"

    $pcpu + $ppid + $pname + $pprio + $ptime + $pmem
}

function XymonDate
{
    "[date]"
    UnixDate $localdatetime
}

function XymonClock
{
    $epoch = epochTimeUtc $localdatetime

    "[clock]"
    "epoch: " + $epoch
    "local: " + (UnixDate $localdatetime)
    "UTC: " + (UnixDate $localdatetime.ToUniversalTime())
    $timesource = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\Parameters').Type
    "Time Synchronisation type: " + $timesource
    if ($timesource -eq "NTP") {
        "NTP server: " + (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\Parameters').NtpServer
    }
    $w32qs = w32tm /query /status  # will not run on 2003, XP or earlier
    if($?) { $w32qs }
}

function XymonUptime
{
    "[uptime]"
    "sec: " + [string] ([int]($uptime.Ticks / 10000000))
    ([string]$uptime.Days) + " days " + ([string]$uptime.Hours) + " hours " + ([string]$uptime.Minutes) + " minutes " + ([string]$uptime.Seconds) + " seconds"
    "Bootup: " + $osinfo.LastBootUpTime
}

function XymonUname
{
    "[uname]"
    $osinfo.Caption + " " + $osinfo.CSDVersion + " (build " + $osinfo.BuildNumber + ")"
}

function XymonClientVersion
{
    "[clientversion]"
    $Version
}

function XymonProcesses
{
    # gather process and timing information and add this to $script:procs
    # variable
    # XymonCpu and XymonProcs use this information to output
 
    WriteLog "XymonProcesses start"

    foreach ($p in $script:procs)
    {
        if ($svcprocs[($p.Id)] -ne $null) {
            $procname = "SVC:" + $svcprocs[($p.Id)]
        }
        else {
            $procname = $p.Name
        }
           
        Add-Member -MemberType NoteProperty `
            -Name XymonProcessName -Value $procname `
            -InputObject $p

        $thisp = $script:XymonProcsCpu[$p.Id]
        if ($thisp -ne $null -and $thisp[3] -eq $true) 
        {
            if ($script:XymonProcsCpuElapsed -gt 0)
            {
                $usedpct = ([int](10000*($thisp[2] / $script:XymonProcsCpuElapsed))) / 100
            }
            else
            {
                $usedpct = 0
            }
            Add-Member -MemberType NoteProperty `
                -Name CommandLine -Value $thisp[4] `
                -InputObject $p
            Add-Member -MemberType NoteProperty `
                -Name Owner -Value $thisp[5] `
                -InputObject $p
        }
        else 
        {
            $usedpct = 0
        }

        Add-Member -MemberType NoteProperty `
            -Name CPUPercent -Value $usedpct `
            -InputObject $p

        $elapsedRuntime = 0
        if ($p.StartTime -ne $null)
        {
            $elapsedRuntime = ($script:localdatetime - $p.StartTime).TotalMinutes 
        }
        Add-Member -MemberType NoteProperty `
            -Name ElapsedSinceStart -Value $elapsedRuntime `
            -InputObject $p

        $pws     = "{0,8:F0}/{1,-8:F0}" -f ($p.WorkingSet64 / 1KB), ($p.PeakWorkingSet64 / 1KB)
        $pvmem   = "{0,8:F0}/{1,-8:F0}" -f ($p.VirtualMemorySize64 / 1KB), ($p.PeakVirtualMemorySize64 / 1KB)
        $ppgmem  = "{0,8:F0}/{1,-8:F0}" -f ($p.PagedMemorySize64 / 1KB), ($p.PeakPagedMemorySize64 / 1KB)
        $pnpgmem = "{0,8:F0}" -f ($p.NonPagedSystemMemorySize64 / 1KB)

        Add-Member -MemberType NoteProperty `
            -Name XymonPeakWorkingSet -Value $pws `
            -InputObject $p
        Add-Member -MemberType NoteProperty `
            -Name XymonPeakVirtualMem -Value $pvmem `
            -InputObject $p
        Add-Member -MemberType NoteProperty `
            -Name XymonPeakPagedMem -Value $ppgmem `
            -InputObject $p
        Add-Member -MemberType NoteProperty `
            -Name XymonNonPagedSystemMem -Value $pnpgmem `
            -InputObject $p
    }

    WriteLog "XymonProcesses finished."
}


function XymonCpu
{
    WriteLog "XymonCpu start"

    $totalcpu = ($script:procs | Measure-Object -Sum -Property CPUPercent | Select -ExpandProperty Sum)
    $totalcpu = [Math]::Round($totalcpu, 2)

    "[cpu]"
    "up: {0} days, {1} users, {2} procs, load={3}%" -f [string]$uptime.Days, $usercount, $procs.count, [string]$totalcpu
    ""
    "CPU states:"
    "`ttotal`t{0}`%" -f [string]$totalcpu
    "`tcores: {0}" -f [string]$script:numcores

    if ($script:XymonProcsCpuElapsed -gt 0) {
        ""
        "CPU".PadRight(9) + "PID".PadRight(8) + "Image Name".PadRight(32) + "Pri".PadRight(5) + "Time".PadRight(9) + "MemUsage"

        $script:procs | Sort-Object -Descending { $_.CPUPercent } `
            | foreach `
            { 
                $skipFlag = $false
                if ($script:clientlocalcfg_entries.ContainsKey('slimmode'))
                {
                    if ($script:clientlocalcfg_entries.slimmode.ContainsKey('processes'))
                    {
                        # skip this process if we are in slimmode and this process is not one of the 
                        # requested processes
                        if ($script:clientlocalcfg_entries.slimmode.processes -notcontains $_.XymonProcessName)
                        {
                            $skipFlag = $true
                        }
                    }
                }
                
                if (!$skipFlag)
                {
                    XymonPrintProcess $_ $_.XymonProcessName $_.CPUPercent 
                }
            }
    }
    WriteLog "XymonCpu finished."
}

function XymonDisk
{
    $MountpointWidth = 10
    $LabelWidth = 10
    $FilesysWidth = 10

    # work out column widths
    foreach ($d in $script:disks)
    {
        $mplength = "/FIXED/$($d.MountPoint)".Length
        if ($mplength -gt $MountpointWidth)
        {
            $MountpointWidth = $mplength
        }
        if ($d.FileSys.Length -gt $FilesysWidth)
        {
            $FilesysWidth = $d.FileSys.Length
        }
        if ($d.VolumeName.Length -gt $LabelWidth)
        {
            $LabelWidth = $d.VolumeName.Length
        }
    }

    WriteLog "XymonDisk start"
    "[disk]"
    "{0,-$FilesysWidth} {1,12} {2,12} {3,12} {4,9}  {5,-$MountpointWidth} {6,-$LabelWidth} {7}" -f `
        "Filesystem", `
        "1K-blocks", `
        "Used", `
        "Avail", `
        "Capacity", `
        "Mounted", `
        "Label", `
        "Summary(Total\Avail GB)"
    foreach ($d in $script:disks) {
        $diskusedKB = $d.UsedBytesKB
        $disksizeKB = $d.TotalBytesKB

        $dsKB = "{0:F0}" -f ($d.TotalBytes / 1KB); $dsGB = "{0:F2}" -f ($d.TotalBytes / 1GB)
        $duKB = "{0:F0}" -f ($diskusedKB); $duGB = "{0:F2}" -f ($diskusedKB / 1KB);
        $dfKB = "{0:F0}" -f ($d.FreeBytes / 1KB); $dfGB = "{0:F2}" -f ($d.FreeBytes / 1GB)

        $mountpoint = "/FIXED/$($d.MountPoint)"
       
        "{0,-$FilesysWidth} {1,12} {2,12} {3,12} {4,9:0}% {5,-$MountpointWidth} {6,-$LabelWidth} {7}" -f `
            $d.FileSys, `
            $dsKB, `
            $duKB, `
            $dfKB, `
            $d.UsedPercent, `
            $mountpoint, `
            $d.VolumeName, `
            $dsGB + "\" + $dfGB
    }

    $script:diskpartData

    WriteLog "XymonDisk finished."
}

function XymonMemory
{
    WriteLog "XymonMemory start"
    $physused  = [int](($osinfo.TotalVisibleMemorySize - $osinfo.FreePhysicalMemory)/1KB)
    $phystotal = [int]($osinfo.TotalVisibleMemorySize / 1KB)
    $pageused  = [int](($osinfo.SizeStoredInPagingFiles - $osinfo.FreeSpaceInPagingFiles) / 1KB)
    $pagetotal = [int]($osinfo.SizeStoredInPagingFiles / 1KB)
    $virtused  = [int](($osinfo.TotalVirtualMemorySize - $osinfo.FreeVirtualMemory) / 1KB)
    $virttotal = [int]($osinfo.TotalVirtualMemorySize / 1KB)

    "[memory]"
    "memory    Total    Used"
    "physical: $phystotal $physused"
    if($script:XymonSettings.clientbbwinmembug -eq 0) {     # 0 = report correctly, 1 = page and virtual switched
        "virtual: $virttotal $virtused"
        "page: $pagetotal $pageused"
    } else {
        "virtual: $pagetotal $pageused"
        "page: $virttotal $virtused"
    }
    WriteLog "XymonMemory finished."
}

# ContainsLike - whether or not $compare matches
# one of the entries in $arrayOfLikes using the -like operator
function ContainsLike([string[]] $arrayOfLikes, [string] $compare)
{
    foreach ($l in $arrayOfLikes)
    {
        if ($compare -like $l)
        {
            return $true
        }
    }
    return $false
}

function XymonMsgs
{
    if ($script:XymonSettings.reportevt -eq 0) {return}

    $sinceMs = (New-Timespan -Minutes $script:XymonSettings.maxlogage).TotalMilliseconds

    # xml template
    #   {0} = log name e.g. Application
    #   {1} = milliseconds - how far back in time to go
    $filterXMLTemplate = `
@' 
    <QueryList>
      <Query Id="0" Path="{0}">
        <Select Path="{0}">*[System[TimeCreated[timediff(@SystemTime) &lt;= {1}] and ({2})]]</Select>
      </Query>
    </QueryList>
'@

    $eventLevels = @{ 
        '0' = 'Information';
        '1' = 'Critical';
        '2' = 'Error';
        '3' = 'Warning';
        '4' = 'Information';
        '5' = 'Verbose';
    }

    # default logs - may be overridden by config
    $wantedlogs = "Application", "System", "Security"
    $wantedLevels = @('Critical', 'Warning', 'Error', 'Information', 'Verbose')
    $maxpayloadlength = 1024
    $payload = ''

    # this function no longer uses $script:XymonSettings.wantedlogs
    # - it now uses eventlogswanted from the remote config
    if ($script:clientlocalcfg_entries.keys | where { $_ -match '^eventlogswanted:(.+):(\d+):?(.+)?$' })
    {
        $wantedlogs = $matches[1] -split ','
        $maxpayloadlength = $matches[2]
        if ($matches[3] -ne $null)
        {
            $wantedLevels = $matches[3] -split ','
        }
    }

    $levelcriteria = @()
    foreach ($level in $wantedLevels)
    {
        switch ($level)
        {
            'critical' { $levelcriteria += 'Level=1'; break }
            'warning' { $levelcriteria += 'Level=3'; break }
            'verbose' { $levelcriteria += 'Level=5'; break }
            'error' { $levelcriteria += 'Level=2'; break }
            'information' { $levelcriteria += 'Level=4 or Level=0'; break }
        }
    }

    WriteLog "Event Log processing - max payload: $maxpayloadlength - wanted logs: $wantedlogs - wanted levels: $wantedLevels"

    foreach ($l in ($script:EventLogs | select -ExpandProperty Log))
    {
        if (ContainsLike $wantedlogs $l)
        {
            WriteLog "Event log $l adding to payload"
            $payload += "[msgs:eventlog_$l]" + [environment]::newline

            # only scan the current log if there is space in the payload
            if ($payload.Length -lt $maxpayloadlength)
            {
                WriteLog "Processing event log $l"

                $logFilterXML = $filterXMLTemplate -f $l, $sinceMs, ($levelcriteria -join ' or ')
                WriteLog "Log filter $logFilterXML"
                
                try
                {
                    WriteLog 'Setting thread/UI culture to en-US'
                    $currentCulture = [System.Threading.Thread]::CurrentThread.CurrentCulture
                    $currentUICulture = [System.Threading.Thread]::CurrentThread.CurrentUICulture
                    [System.Threading.Thread]::CurrentThread.CurrentCulture = 'en-US'
                    [System.Threading.Thread]::CurrentThread.CurrentUICulture = 'en-US'

                    # todo - make this max events number configurable
                    $logentries = @(Get-WinEvent -ErrorAction:SilentlyContinue -FilterXML $logFilterXML `
                        -MaxEvents $script:XymonSettings.MaxEvents)
                }
                catch
                {
                    WriteLog "Error setting culture and getting event log entries: $_"
                }
                finally
                {
                    WriteLog "Resetting thread/UI culture to previous: $currentCulture / $currentUICulture"
                    [System.Threading.Thread]::CurrentThread.CurrentCulture = $currentCulture
                    [System.Threading.Thread]::CurrentThread.CurrentUICulture = $currentUICulture
                }

                WriteLog "Event log $l entries since last scan: $($logentries.Length)"
                
                # filter based on clientlocal.cfg / clientconfig.cfg
                if ($script:clientlocalcfg_entries -ne $null)
                {
                    $filterkey = $script:clientlocalcfg_entries.keys | where { $_ -match "^eventlog\:$l" }
                    if ($filterkey -ne $null -and $script:clientlocalcfg_entries.ContainsKey($filterkey))
                    {
                        WriteLog "Found a configured filter for log $l"
                        $output = @()
                        foreach ($entry in $logentries)
                        {
                            foreach ($filter in $script:clientlocalcfg_entries[$filterkey])
                            {
                                if ($filter -match '^ignore')
                                {
                                    $filter = $filter -replace '^ignore ', ''
                                    if ($entry.ProviderName -match $filter -or $entry.Message -match $filter)
                                    {
                                        $exclude = $true
                                        break
                                    }
                                }
                            }
                            if (-not $exclude)
                            {
                                $output += $entry
                            }
                        }
                        $logentries = $output
                    }
                }

                if ($logentries -ne $null) 
                {
                    foreach ($entry in $logentries) 
                    {
                        $level = 'Unknown'
                        if ($eventLevels.ContainsKey($entry.Level.ToString()))
                        {
                            $level = $eventLevels[$entry.Level.ToString()]
                        }
                        $payload += [string]$level + " - " +`
                            [string]$entry.TimeCreated + " - " + `
                            "[$($entry.Id)] - " + `
                            [string]$entry.ProviderName + " - " + `
                            [string]$entry.Message + [environment]::newline
                        
                        if ($payload.Length -gt $maxpayloadlength)
                        {
                            break;
                        }
                    }
                }
            }
        }
    }
    WriteLog "Event log processing finished"
    $payload
}

function ResolveEnvPath($envpath)
{
    $s = $envpath
    while($s -match '%([\w_]+)%') {
        if(! (test-path "env:$($matches[1])")) { return $envpath }
        $s = $s.Replace($matches[0],$(Invoke-Expression "`$env:$($matches[1])"))
    }
    if(! (test-path $s)) { return $envpath }
    resolve-path $s | Select -ExpandProperty ProviderPath
}

function XymonDir
{
    #$script:clientlocalcfg | ? { $_ -match "^dir:(.*)" } | % {
    $script:clientlocalcfg_entries.keys | where { $_ -match "^dir:(.*)" } |`
        foreach {
        resolveEnvPath $matches[1] | foreach {
            "[dir:$($_)]"
            if(test-path $_ -PathType Container) { du $_ }
            elseif(test-path $_) {"ERROR: The path specified is not a directory." }
            else { "ERROR: The system cannot find the path specified." }
        }
    }
}

function XymonFileStat($file,$hash="")
{
    # don't implement hashing yet - don't even check for it...
    if(test-path $_) {
        $fh = get-item $_
        if(test-path $_ -PathType Leaf) {
            "type:100000 (file)"
        } else {
            "type:40000 (directory)"
        }
        "mode:{0} (not implemented)" -f $(if($fh.IsReadOnly) {555} else {777})
        "linkcount:1"
        "owner:0 ({0})" -f $fh.GetAccessControl().Owner
        "group:0 ({0})" -f $fh.GetAccessControl().Group
        if(test-path $_ -PathType Leaf) { "size:{0}" -f $fh.length }
        "atime:{0} ({1})" -f (epochTimeUtc $fh.LastAccessTimeUtc),$fh.LastAccessTime.ToString("yyyy/MM/dd-HH:mm:ss")
        "ctime:{0} ({1})" -f (epochTimeUtc $fh.CreationTimeUtc),$fh.CreationTime.ToString("yyyy/MM/dd-HH:mm:ss")
        "mtime:{0} ({1})" -f (epochTimeUtc $fh.LastWriteTimeUtc),$fh.LastWriteTime.ToString("yyyy/MM/dd-HH:mm:ss")
        if(test-path $_ -PathType Leaf) {
            "FileVersion:{0}" -f $fh.VersionInfo.FileVersion
            "FileDescription:{0}" -f $fh.VersionInfo.FileDescription
        }
    } else {
        "ERROR: The system cannot find the path specified."
    }
}

function XymonFileCheck
{
    # don't implement hashing yet - don't even check for it...
    #$script:clientlocalcfg | ? { $_ -match "^file:(.*)$" } | % {
    $script:clientlocalcfg_entries.keys | where { $_ -match "^file:(.*)$" } |`
        foreach {
        resolveEnvPath $matches[1] | foreach {
            "[file:$_]"
            XymonFileStat $_
        }
    }
}

function XymonLogCheck
{
    #$script:clientlocalcfg | ? { $_ -match "^log:(.*):(\d+)$" } | % {
    $script:clientlocalcfg_entries.keys | where { $_ -match "^log:([a-z%][a-z:][^:]+):(\d+):?(\d+)?$" } |`
        foreach {
        $positions = 6
        if ($matches[3] -ne $null)
        {
            $positions = $matches[3]
        }
        $sizemax = $matches[2]
        resolveEnvPath $matches[1] | foreach {
            "[logfile:$_]"
            XymonFileStat $_
            "[msgs:$_]"
            XymonLogCheckFile $_ $sizemax $positions
        }
    }
}

function XymonLogCheckFile([string]$file,$sizemax=0, $positions=6)
{
    WriteLog "Executing XymonLogCheckFile"
    WriteLog "File: $file"
    $f = [system.io.file]::Open($file,"Open","Read","ReadWrite")
    $s = get-item $file
    $nowpos = $f.length
    $savepos = 0
    if($script:logfilepos.$($file) -ne $null) { $savepos = $script:logfilepos.$($file)[0] }
    if($nowpos -lt $savepos) {$savepos = 0} # log file rolled over??
    #"Save: {0}  Len: {1} Diff: {2} Max: {3} Write: {4}" -f $savepos,$nowpos, ($nowpos-$savepos),$sizemax,$s.LastWriteTime
    if($nowpos -gt $savepos) { # must be some more content to check
        $s = new-object system.io.StreamReader($f,$true)
        $dummy = $s.readline()
        $enc = $s.currentEncoding
        $charsize = 1
        if($enc.EncodingName -eq "Unicode") { $charsize = 2 }
        if($nowpos-$savepos -gt $charsize*$sizemax) {$savepos = $nowpos-$charsize*$sizemax}
        $seek = $f.Seek($savepos,0)
        $t = new-object system.io.StreamReader($f,$enc)
        $buf = $t.readtoend()
        if($buf -ne $null) { $buf }
        #"Save2: {0}  Pos: {1} Blen: {2} Len: {3} Enc($charsize): {4}" -f $savepos,$f.Position,$buf.length,$nowpos,$enc.EncodingName
    }
    if($script:logfilepos.$($file) -ne $null) {
        $script:logfilepos.$($file) = $script:logfilepos.$($file)[1..$positions]
    } else {
        $script:logfilepos.$($file) = @(0) * $positions
    }
    $script:logfilepos.$($file) += $nowpos # save for next loop
    WriteLog ("File saved positions: " + ($script:logfilepos.$($file) -join ','))
    WriteLog "XymonLogCheckFile finished"
}

function XymonDirSize
{
    # dirsize:<path>:<gt/lt/eq>:<size bytes>:<fail colour>
    # match number:
    #        :  1   :   2      :     3      :     4
    # <path> may be a simple path (c:\temp) or contain an environment variable, or a filename
    # e.g. %USERPROFILE%\temp
    WriteLog "Executing XymonDirSize"
    $outputtext = ''
    $groupcolour = 'green'
    $script:clientlocalcfg_entries.keys | where { $_ -match '^dirsize:([a-z%][a-z:][^:]+):([gl]t|eq):(\d+):(.+)$' } |`
        foreach {
            resolveEnvPath $matches[1] | foreach {

                WriteLog "DirSize: $_"
                $objFSO = new-object -com Scripting.FileSystemObject

                if (test-path $_ -PathType Container)
                {
                    # could use "get-childitem ... -recurse | measure ..." here 
                    # but that does not work well when there are many files/subfolders
                    $size = $objFSO.GetFolder($_).Size
                }
                elseif (test-path $_)
                {
                    $size = (Get-Item $_).Length
                }
                else
                {
                    # file / directory does not exist
                    WriteLog "File $_ not found, setting size = -1"
                    $size = -1
                }

                $criteriasize = ($matches[3] -as [long])
                $conditionmet = $false
                if ($matches[2] -eq 'gt')
                {
                    $conditionmet = $size -gt $criteriasize
                    $conditiontype = '>'
                }
                elseif ($matches[2] -eq 'lt')
                {
                    $conditionmet = $size -lt $criteriasize
                    $conditiontype = '<'
                }
                else
                {
                    # eq
                    $conditionmet = $size -eq $criteriasize
                    $conditiontype = '='
                }
                if ($conditionmet)
                {
                    $alertcolour = $matches[4]
                }
                else
                {
                    $alertcolour = 'green'
                }

                # report out - 
                #  {0} = colour (matches[4])
                #  {1} = folder name
                #  {2} = folder size
                #  {3} = condition symbol (<,>,=)
                #  {4} = alert size
                $outputtext += (('<img src="{5}{0}.gif" alt="{0}" ' +`
                    'height="16" width="16" border="0">' +`
                    '{1} size is {2} bytes. Alert if {3} {4} bytes.<br>') `
                    -f $alertcolour, $_, $size, $conditiontype, $matches[3], $script:XymonSettings.servergiflocation)
                # set group colour to colour if it is not already set to a 
                # higher alert state colour
                if ($groupcolour -eq 'green' -and $alertcolour -eq 'yellow')
                {
                    $groupcolour = 'yellow'
                }
                elseif ($alertcolour -eq 'red')
                {
                    $groupcolour = 'red'
                }
            }
        }

    if ($outputtext -ne '')
    {
        $outputtext = (get-date -format G) + '<br><h2>Directory Size</h2>' + $outputtext
        $output = ('status {0}.dirsize {1} {2}' -f $script:clientname, $groupcolour, $outputtext)
        WriteLog "dirsize: Sending $output"
        XymonSend $output $script:XymonSettings.serversList
    }
}

function XymonDirTime
{
    # dirtime:<path>:<unused>:<gt/lt/eq>:<alerttime>:<colour>
    # match number:
    #        :  1   :    2   :     3    :     4     :   5
    # <path> may be a simple path (c:\temp) or contain an environment variable
    # e.g. %USERPROFILE%\temp
    # <alerttime> = number of minutes to alert after
    # e.g. if a directory should be modified every 10 minutes
    # alert for gt 10
    WriteLog "Executing XymonDirTime"
    $outputtext = ''
    $groupcolour = 'green'
    $script:clientlocalcfg_entries.keys | where { $_ -match '^dirtime:([a-z%][a-z:][^:]+):([^:]*):([gl]t|eq):(\d+):(.+)$' } |`
        foreach {
            resolveEnvPath $matches[1] | foreach {

                $skip = $false
                WriteLog "DirTime: $_"
                try
                {
                    $minutesdiff = ((get-date) - (Get-Item $_ -ErrorAction Stop).LastWriteTime).TotalMinutes
                }
                catch 
                {
                    $outputtext += (('<img src="{2}{0}.gif" alt="{0}"' +`
                        'height="16" width="16" border="0">' +`
                        '{1}') `
                        -f 'red', $_, $script:XymonSettings.servergiflocation)
                    $groupcolour = 'red'
                    $skip = $true
                }
                if (-not $skip)
                {
                    $criteriaminutes = ($matches[4] -as [int])
                    $conditionmet = $false
                    if ($matches[3] -eq 'gt')
                    {
                        $conditionmet = $minutesdiff -gt $criteriaminutes
                        $conditiontype = '>'
                    }
                    elseif ($matches[3] -eq 'lt')
                    {
                        $conditionmet = $minutesdiff -lt $criteriaminutes
                        $conditiontype = '<'
                    }
                    else
                    {
                        $conditionmet = $minutesdiff -eq $criteriaminutes
                        $conditiontype = '='
                    }
                    if ($conditionmet)
                    {
                        $alertcolour = $matches[5]
                    }
                    else
                    {
                        $alertcolour = 'green'
                    }
                    # report out - 
                    #  {0} = colour (matches[5])
                    #  {1} = folder name
                    #  {2} = folder modified x minutes ago
                    #  {3} = condition symbol (<,>,=)
                    #  {4} = alert criteria minutes
                    $outputtext += (('<img src="{5}{0}.gif" alt="{0}"' +`
                        'height="16" width="16" border="0">' +`
                        '{1} updated {2:F1} minutes ago. Alert if {3} {4} minutes ago.<br>') `
                        -f $alertcolour, $_, $minutesdiff, $conditiontype, $criteriaminutes, $script:XymonSettings.servergiflocation)
                    # set group colour to colour if it is not already set to a 
                    # higher alert state colour
                    if ($groupcolour -eq 'green' -and $alertcolour -eq 'yellow')
                    {
                        $groupcolour = 'yellow'
                    }
                    elseif ($alertcolour -eq 'red')
                    {
                        $groupcolour = 'red'
                    }
                }
            }
        }

    if ($outputtext -ne '')
    {
        $outputtext = (get-date -format G) + '<br><h2>Last Modified Time In Minutes</h2>' + $outputtext
        $output = ('status {0}.dirtime {1} {2}' -f $script:clientname, $groupcolour, $outputtext)
        WriteLog "dirtime: Sending $output"
        XymonSend $output $script:XymonSettings.serversList
    }
}

function XymonPorts
{
    WriteLog "XymonPorts start"
    "[ports]"
    netstat -an
    WriteLog "XymonPorts finished."
}

function XymonIpconfig
{
    WriteLog "XymonIpconfig start"
    "[ipconfig]"
    ipconfig /all | %{ $_.split("`n") } | ?{ $_ -match "\S" }  # for some reason adds blankline between each line
    WriteLog "XymonIpconfig finished."
}

function XymonRoute
{
    WriteLog "XymonRoute start"
    "[route]"
    netstat -rn
    WriteLog "XymonRoute finished."
}

function XymonNetstat
{
    WriteLog "XymonNetstat start"
    "[netstat]"
    $pref=""
    netstat -s | ?{$_ -match "=|(\w+) Statistics for"} | %{ if($_.contains("=")) {("$pref$_").REPLACE(" ","")}else{$pref=$matches[1].ToLower();""}}
    WriteLog "XymonNetstat finished."
}

function XymonIfstat
{
    WriteLog "XymonIfstat start"
    $families = @{ 'IPv4' = [System.Net.Sockets.AddressFamily]::InterNetwork; 
        'IPv6' = [System.Net.Sockets.AddressFamily]::InterNetworkV6;
    }

    $wantedFamilies = @()
    $script:clientlocalcfg_entries.keys | where { $_ -match '^ifstat:((ipv[46],?)+)$' } |
        foreach {
            foreach ($wanted in ($matches[1] -split ','))
            {
                if ($families.ContainsKey($wanted))
                {
                    $wantedFamilies += $families[$wanted]
                }
            }
            $wantedFamilies = ($wantedFamilies | Sort-Object -Unique)
        }
    if (@($wantedFamilies).Length -eq 0)
    {
        $wantedFamilies += $families['IPv4']
    }
    WriteLog "wanted address families: $wantedFamilies"

    "[ifstat]"
    [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | 
        where { $_.OperationalStatus -eq "Up" -and $_.NetworkInterfaceType -ne 'loopback' } |
        foreach {
        $ad = $_.GetIPv4Statistics() | select BytesSent, BytesReceived
        $ip = $_.GetIPProperties().UnicastAddresses | select Address
        # some interfaces have multiple IPs, so iterate over them reporting same stats
        # also replace statement removes zone information (adaptor) from IPv6 addresses
        $ip | where { $wantedFamilies -contains $_.Address.AddressFamily } |
            foreach { "{0} {1} {2}" -f ($_.Address.IPAddressToString -replace '%\d+$'),$ad.BytesReceived,$ad.BytesSent }
    }
    WriteLog "XymonIfstat finished."
}

function XymonSvcs
{
    WriteLog "XymonSvcs start"
    if ($script:clientlocalcfg_entries.ContainsKey('slimmode') -and `
        !$script:clientlocalcfg_entries.slimmode.ContainsKey('services'))
    {
        WriteLog 'Skipping XymonSvcs; slim mode but no services specified'
        return
    }
    "[svcs]"
    "Name".PadRight(39) + " " + "StartupType".PadRight(12) + " " + "Status".PadRight(14) + " " + "DisplayName"
    foreach ($s in $svcs) 
    {
        if ($script:clientlocalcfg_entries.ContainsKey('slimmode'))
        {
            if ($script:clientlocalcfg_entries.slimmode.ContainsKey('services'))
            {
                # skip this service if we are in slimmode and this service is not one of the 
                # requested services
                if ($script:clientlocalcfg_entries.slimmode.services -notcontains $s.Name)
                {
                    continue
                }
            }
        }
        if ($s.StartMode -eq "Auto") { $stm = "automatic" } else { $stm = $s.StartMode.ToLower() }
        if ($s.State -eq "Running")  { $state = "started" } else { $state = $s.State.ToLower() }
        $s.Name.Replace(" ","_").PadRight(39) + " " + $stm.PadRight(12) + " " + $state.PadRight(14) + " " + $s.DisplayName
    }
    WriteLog "XymonSvcs finished."
}

function XymonProcs
{
    WriteLog "XymonProcs start"
    "[procs]"
    "{0,8} {1,-35} {2,-17} {3,-17} {4,-17} {5,8} {6,-7} {7,5} {8,-19} {9,7} {10} {11}" -f `
        "PID", "User", "WorkingSet/Peak", "VirtualMem/Peak", "PagedMem/Peak", "NPS", `
        "Handles", "%CPU", 'Start Time', 'Elapsed', "Name", "Command"
    
    # output sorted process table
    $script:procs | Sort-Object -Descending { $_.CPUPercent } `
        | foreach {
            $startTime = ''
            if ($_.StartTime -ne $null)
            {
                $startTime = Get-Date -Date $_.StartTime -uformat '%Y-%m-%d %H:%M:%S'
            }

            $skipFlag = $false
            if ($script:clientlocalcfg_entries.ContainsKey('slimmode'))
            {
                if ($script:clientlocalcfg_entries.slimmode.ContainsKey('processes'))
                {
                    # skip this process if we are in slimmode and this process is not one of the 
                    # requested processes
                    if ($script:clientlocalcfg_entries.slimmode.processes -notcontains $_.XymonProcessName)
                    {
                        $skipFlag = $true
                    }
                }
            }
            
            if (!$skipFlag)
            {
                "{0,8} {1,-35} {2} {3} {4} {5} {6,7:F0} {7,5:F1} {8,19} {9,7:F0} {10} {11}" -f $_.Id, $_.Owner, `
                    $_.XymonPeakWorkingSet, $_.XymonPeakVirtualMem,`
                     $_.XymonPeakPagedMem, $_.XymonNonPagedSystemMem, `
                     $_.Handles, $_.CPUPercent, `
                     $startTime, $_.ElapsedSinceStart, $_.XymonProcessName, $_.CommandLine
            }
    }
    WriteLog "XymonProcs finished."
}

function CleanXymonProcsCpu
{
    # reset cache flags and clear terminated processes from the cache
    WriteLog "CleanXymonProcsCpu start"
    if ($script:XymonProcsCpu.Count -gt 0)
    {
        foreach ($p in @($script:XymonProcsCpu.Keys)) {
            $thisp = $script:XymonProcsCpu[$p]
            if ($thisp[3] -eq $true) {
                # reset flag to catch a dead process on the next run
                # this flag will be updated back to $true by XymonProcsCPUUtilisation
                # if the process still exists
                $thisp[3] = $false  
            }
            else {
                # flag was set to $false previously = process has been terminated
                WriteLog "Process id $p has disappeared, removing from cache"
                $script:XymonProcsCpu.Remove($p)
            }
        }
    }
    WriteLog ("DEBUG: cached process ids: " + (($script:XymonProcsCpu.Keys | sort-object) -join ', '))
    WriteLog "CleanXymonProcsCpu finished."
}

function XymonWho
{
    WriteLog "XymonWho start"
    if( $HaveCmd.qwinsta) 
    {
        "[who]"
        if ($script:usersessions -eq $null)
        {
            qwinsta.exe /counter
        }
        else
        {
            $script:usersessions
        }
    }
    WriteLog "XymonWho finished."
}

function XymonUsers
{
    WriteLog "XymonUsers start"
    if( $HaveCmd.query) {
        "[users]"
        query user
    }
    WriteLog "XymonUsers finished."
}

function XymonIISSites
{
    WriteLog "XymonIISSites start"
    $objSites = [adsi]("IIS://localhost/W3SVC")
    if($objSites.path -ne $null) {
        "[iis_sites]"
        foreach ($objChild in $objSites.Psbase.children | where {$_.KeyType -eq "IIsWebServer"} ) {
            ""
            $objChild.servercomment
            $objChild.path
            if($objChild.path -match "\/W3SVC\/(\d+)") { "SiteID: "+$matches[1] }
            foreach ($prop in @("LogFileDirectory","LogFileLocaltimeRollover","LogFileTruncateSize","ServerAutoStart","ServerBindings","ServerState","SecureBindings" )) {
                if( $($objChild | gm -Name $prop ) -ne $null) {
                    "{0} {1}" -f $prop,$objChild.$prop.ToString()
                }
            }
        }
        clear-variable objChild
    }
    clear-variable objSites
    WriteLog "XymonIISSites finished."
}

function XymonWMIOperatingSystem
{
    "[WMI:Win32_OperatingSystem]"
    WMIProp Win32_OperatingSystem
}

function XymonWMIQuickFixEngineering
{
    if ($script:XymonSettings.EnableWin32_QuickFixEngineering -eq 1)
    {
        "[WMI:Win32_QuickFixEngineering]"
        Get-WmiObject -Class Win32_QuickFixEngineering | where { $_.Description -ne "" } | Sort-Object HotFixID | Format-Wide -Property HotFixID -AutoSize
    }
    else
    {
        WriteLog "Skipping XymonWMIQuickFixEngineering, EnableWin32_QuickFixEngineering = 0 in config"
    }
}

function XymonWMIProduct
{
    if ($script:XymonSettings.EnableWin32_Product -eq 1)
    {
        # run as job, since Win32_Product WMI dies on some systems (e.g. XP)
        $job = Get-WmiObject -Class Win32_Product -AsJob | wait-job
        if($job.State -eq "Completed") {
            "[WMI:Win32_Product]"
            $fmt = "{0,-70} {1,-15} {2}"
            $fmt -f "Name", "Version", "Vendor"
            $fmt -f "----", "-------", "------"
            receive-job $job | Sort-Object Name | 
            foreach {
                $fmt -f $_.Name, $_.Version, $_.Vendor
            }
        }
        remove-job $job
    }
    else
    {
        WriteLog "Skipping XymonWMIProduct, EnableWin32_Product = 0 in config"
    }
}

function XymonWMIComputerSystem
{
    "[WMI:Win32_ComputerSystem]"
    WMIProp Win32_ComputerSystem
}

function XymonWMIBIOS
{
    "[WMI:Win32_BIOS]"
    WMIProp Win32_BIOS
}

function XymonWMIProcessor
{
    "[WMI:Win32_Processor]"
    $cpuinfo | Format-List DeviceId,Name,CurrentClockSpeed,NumberOfCores,NumberOfLogicalProcessors,CpuStatus,Status,LoadPercentage
}

function XymonWMIMemory
{
    "[WMI:Win32_PhysicalMemory]"
    Get-WmiObject -Class Win32_PhysicalMemory | Format-Table -AutoSize BankLabel,Capacity,DataWidth,DeviceLocator
}

function XymonWMILogicalDisk
{
    "[WMI:Win32_LogicalDisk]"
    Get-WmiObject -Class Win32_LogicalDisk | Format-Table -AutoSize
}

function XymonDiskPart
{
    WriteLog 'XymonDiskPart start'

    try
    {
        $diskpart = 'list disk' | diskpart
        $dpOutput = $diskpart | where { $_ -match '^  Disk \d+' }
        $dpOutput = $dpOutput -replace '^\s+', ''
        $dpOutput = $dpOutput -replace '\s+$', ''
        "[diskpart]"
        
        $diskDetailCmd = "select disk {0}`r`ndetail disk"
        $noVolumeRX = '^There are no volumes.'

        $dpOutput | foreach {
            $dpColumns = $_ -split '\s{2,}'
            $diskNum = $dpColumns[0] -replace 'Disk ', ''
            $cmd = $diskDetailCmd -f $diskNum
            $detailOutput = $cmd | diskpart
            $detailDisk = $detailOutput | where { $_ -match '^Clustered' -or $_ -match $noVolumeRX }
        
            if ($detailDisk -match '^Clustered Disk  : No')
            {
                $clusterOutput = 'Not Clustered'
            }
            else
            {
                if (-not ($detailDisk -match '^Clustered'))
                {
                    $clusterOutput = 'Clustered Unknown'
                }
                else
                {
                    $clusterOutput = 'Clustered Active'
                    if ($detailDisk -match $noVolumeRX)
                    {
                        $clusterOutput = 'Clustered Inactive'
                    }
                }
            }

            "diskpart:{0}:{1}:{2}" -f $dpColumns[0], $dpColumns[2], $clusterOutput
        }
    }
    catch
    {
        WriteLog "Xymondisk diskpart - error $_"
    }

    WriteLog 'XymonDiskPart finished'
}
function XymonEventLogs
{
    if ($script:XymonSettings.reportevt -eq 0) {return}

    "[EventlogSummary]"
    $script:EventLogs = Get-EventLog -List 
    $script:EventLogs | Format-Table -AutoSize

    "[msgs:EventlogSummary]"
    $script:EventLogs | Format-Table -AutoSize
}

function XymonServiceCheck
{
    WriteLog "Executing XymonServiceCheck"
    if ($script:clientlocalcfg_entries -ne $null)
    {
        $servicecfgs = @($script:clientlocalcfg_entries.keys | where { $_ -match '^servicecheck' })
        foreach ($service in $servicecfgs)
        {
            # parameter should be 'servicecheck:<servicename>:<duration>'
            $checkparams = $service -split ':'
            # validation
            if ($checkparams.length -ne 3)
            {
                WriteLog "ERROR: config error (should be servicecheck:<servicename>:<duration>) - $service"
                continue
            }
            else
            {
                $duration = $checkparams[2] -as [int]
                if ($checkparams[1] -eq '' -or $duration -eq $null)
                {
                    WriteLog "ERROR: config error (should be servicecheck:<servicename>:<duration>) - $service"
                    continue
                }
            }

            WriteLog ("Checking service {0}" -f $checkparams[1])

            $winsrv = Get-Service -Name $checkparams[1]
            if ($winsrv.Status -eq 'Stopped')
            {
                writeLog ("Service {0} is stopped" -f $checkparams[1])
                if ($script:ServiceChecks.ContainsKey($checkparams[1]))
                {
                    $restarttime = $script:ServiceChecks[$checkparams[1]].AddSeconds($duration)
                    writeLog "Seen this service before; restart time is $restarttime"
                    if ($restarttime -lt (get-date))
                    {
                        writeLog ("Starting service {0}" -f $checkparams[1])
                        $winsrv.Start()
                    }
                }
                else
                {
                    writeLog "Not seen this service before, setting restart time -1 hour"
                    $script:ServiceChecks[$checkparams[1]] = (get-date).AddHours(-1)
                }
            }
            elseif ('StartPending', 'Running' -contains $winsrv.Status)
            {
                writeLog "Service is running, updating last seen time"
                $script:ServiceChecks[$checkparams[1]] = get-date
            }
        }
    }
}

function XymonTerminalServicesSessionsCheck
{
    # this function relies on data from XymonWho - should be called after XymonWho
    WriteLog "Executing XymonTerminalServicesSessionsCheck"

    # config: terminalservicessessions:<yellowthreshold>:<redthreshold>
    # thresholds are number of free sessions - so alert when only x sessions free
    $script:clientlocalcfg_entries.keys | where { $_ -match '^(?:ts|terminalservices)sessions:(\d+):(\d+)' } |`
        foreach {
            try
            {
                $maxSessions = Get-ItemProperty -ErrorAction:Stop `
                    -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RDP-Tcp'`
                    -Name MaxInstanceCount | select -ExpandProperty MaxInstanceCount
            }
            catch
            {
                WriteLog "Failed to get max sessions from CurrentControlSet registry: $_"
                $maxSessions = 0xffffffffL 
            }

            $maxSessionMsg = ''
            if ($maxSessions -eq 0xffffffffL)
            {
                # try group policy key
                try
                {
                    $maxSessions = Get-ItemProperty -ErrorAction:Stop `
                        -Path 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services'`
                        -Name MaxInstanceCount | select -ExpandProperty MaxInstanceCount
                }
                catch
                {
                    WriteLog "Failed to get max sessions from Group Policy registry: $_"
                    return
                }
            }

            if ($maxSessions -eq 0xffffffffL)
            {
                $maxSessionMsg = "Max sessions not set (probably not an RDS server)"
                WriteLog $maxSessionMsg
                $maxSessions = 2
            }

            $yellowThreshold = $matches[1]
            $redThreshold = $matches[2]

            $activeSessions = $script:usersessions | where { $_ -match 'Active' } | measure | 
                select -ExpandProperty Count

            $freeSessions = $maxSessions - $activeSessions

            WriteLog "sessions: active: $activeSessions maximum: $maxSessions free: $freeSessions"
            WriteLog "thresholds: yellow: $yellowThreshold red: $redThreshold"

            $alertColour = 'green'

            if ($freeSessions -le $redThreshold)
            {
                $alertColour = 'red'
            }
            elseif ($freeSessions -le $yellowThreshold)
            {
                $alertColour = 'yellow'
            }

            $outputtext = (('<img src="{0}{1}.gif" alt="{1}" ' +`
                            'height="16" width="16" border="0">' +`
                            'sessions: active: {2} maximum: {3} free: {4}. {7}<br>yellow alert = {5} free, red = {6} free.<br>') `
                            -f $script:XymonSettings.servergiflocation, $alertColour, `
                            $activeSessions, $maxSessions, $freeSessions, $yellowThreshold, $redThreshold, $maxSessionMsg)
            
            $outputtext = (get-date -format G) + '<br><h2>Terminal Services Sessions</h2>' + $outputtext
            $output = ('status {0}.tssessions {1} {2}' -f $script:clientname, $alertColour, $outputtext)
            WriteLog "Terminal Services Sessions: sending $output"
            XymonSend $output $script:XymonSettings.serversList
        }
}

function XymonActiveDirectoryReplicationCheck
{
    WriteLog "Executing XymonActiveDirectoryReplicationCheck"
    if ($script:clientlocalcfg_entries.keys -contains 'adreplicationcheck')
    {
        $status = repadmin /showrepl * /csv
        $results = @(ConvertFrom-Csv -InputObject $status)

        $alertColour = 'green'
    
        $failcount = ($results | where { $_.'Last Failure Time' -gt $_.'Last Success Time' }).Length
        if ($failcount -gt 0)
        {
            $alertColour = 'red'
        }
        else
        {
            $failcount = 'none'
        }
        
        $outputtext = (('<img src="{0}{1}.gif" alt="{1}" ' +`
                        'height="16" width="16" border="0">' +`
                        'Failing replication contexts: {2}<br>red alert = more than zero.<br>') `
                        -f $script:XymonSettings.servergiflocation, $alertColour, `
                        $failcount)
        $outputtext = (get-date -format G) + '<br><h2>Active Directory Replication</h2>' + $outputtext
        $outputtext += '<br/>'

        $outputtable = ($results | select 'Source DSA', `
            'Naming Context', 'Destination DSA', 'Number of Failures', `
            'Last Failure Time', 'Last Success Time', 'Last Failure Status'`
             | ConvertTo-Html -Fragment)

        $outputtable = $outputtable -replace '<table>', '<table style="font-size: 10pt">'

        $outputtext += $outputtable
        $output = ('status {0}.adreplication {1} {2}' -f $script:clientname, $alertColour, $outputtext)
        WriteLog "Active Directory Replication: sending status $alertColour"
        XymonSend $output $script:XymonSettings.serversList
    }
}

function XymonProcessRuntimeCheck
{
    WriteLog 'Executing XymonProcessRuntimeCheck'
    
    # config: processruntime:<process name>:<yellow elapsed threshold>:<red elapsed threshold>
    # thresholds in minutes

    $groupColour = 'green'

    $outputHeader = (get-date -format G) + "<br><h3>Process Run Time Check</h3><pre>"
    $output = ''

    $script:clientlocalcfg_entries.keys | where { $_ -match '^proc(?:ess)?runtime:(.+):(\d+):(\d+)' } | `
        foreach {
            $processName = $matches[1]
            $yellowThreshold = $matches[2]
            $redThreshold = $matches[3]
            $alertColour = 'green'
            $headerColour = 'green'

            $script:procs | where { $_.XymonProcessName -eq $processName } | foreach {
                if ($_.ElapsedSinceStart -gt $redThreshold)
                {
                    $alertColour = 'red'
                    $headerColour = 'red'
                    $groupcolour = 'red'
                }
                elseif ($_.ElapsedSinceStart -gt $yellowThreshold)
                {
                    $alertColour = 'yellow'
                }
                if ($groupcolour -eq 'green' -and $alertcolour -eq 'yellow')
                {
                    $groupcolour = 'yellow'
                }
                if ($headerColour -eq 'green' -and $alertColour -eq 'yellow')
                {
                    $headerColour = 'yellow'
                }

                WriteLog "Process $($_.XymonProcessName) running for $($_.ElapsedSinceStart) minutes: $alertcolour"

                $startTime = Get-Date -Date $_.StartTime -uformat '%Y-%m-%d %H:%M:%S'
                $processLine = "{0,8} {1,-35} {2,-19} {3,7:F0} {4} {5}" -f $_.Id, $_.Owner, `
                     $startTime, $_.ElapsedSinceStart, $_.XymonProcessName, $_.CommandLine

                $output += '<img src="{2}{0}.gif" alt="{0}" height="16" width="16" border="0">{1}<br>' `
                    -f $alertcolour, $processLine, $script:XymonSettings.servergiflocation
            }

            $outputHeader += ('<img src="{1}{0}.gif" alt="{0}" height="16" width="16" border="0">' + `
                'Process: {2}  Yellow alert after {3} minutes, Red alert after {4} minutes<br>') `
                -f $headerColour, $script:XymonSettings.servergiflocation, `
                    $processName, $yellowThreshold, $redThreshold
        }

    if ($output -ne '')
    {
        $output += '</pre>'
        $outputHeader += '<br><span style="margin-left: 16px;">{0,8} {1,-35} {2,19} {3,7} {4} {5}</span><br>' `
            -f "PID", "User", 'Start Time', 'Elapsed', "Name", "Command"
    }
    $output = $outputHeader + $output

    WriteLog "Sending output for procruntime"
    $outputXymon = ('status {0}.procruntime {1} {2}' -f $script:clientname, $groupcolour, $output)
    XymonSend $outputXymon $script:XymonSettings.serversList
    WriteLog 'XymonProcessRuntimeCheck finished'
}

function XymonProcessExternalData
{
    WriteLog 'Executing XymonProcessExternalData'

    if (Test-Path $script:XymonSettings.externaldatalocation)
    {
        $files = Get-ChildItem $script:XymonSettings.externaldatalocation

        if ($files -ne $null)
        {
            foreach ($f in $files)
            {
                # external filenames
                # it appears that BBWin ignores external files containing a dot '.'?
                # so replicate that behaviour
                if ($f.Name -match '\.')
                {
                    continue
                }
                # a valid filename is either just the test name: testname
                # or testname^hostname, to allow sending results from a different 
                # named host
                if ($f.Name -match '^([\w-]+)(?:\^([\S]+))?$')
                {
                    $testName = $matches[1]
                    $hostName = $matches[2]
                
                    if ($hostName -eq $null)
                    {
                        $hostName = $script:clientname
                    }

                    # attempt to open the file with an exclusive lock
                    # if we cannot, the file may be being updated by a running job, so
                    # we will ignore it until the next poll
                    WriteLog "Attempting to process external file $($f.FullName)"
                    try
                    {
                        $statusFile = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'None')
                        $reader = New-Object System.IO.StreamReader($statusFile)
                        $statusFileContent = $reader.ReadToEnd()
                        $reader.Close()
                        $statusFile.Close()
                    }
                    catch
                    {        
                        # if this file is locked or other errors, skip and go to the next one
                        if ($_ -like '*The process cannot access the file*because it is being used by another process*')
                        {
                            WriteLog "External file $($f.Name) is locked by another process, skipping"
                        }
                        else
                        {
                            WriteLog "External file $($f.Name) error accessing file, skipping: $_"
                        }
                        continue
                    }

                    # match:
                    # colour ($matches[1])
                    # optionally + and any non-space chars ($matches[2])
                    # space
                    # remainder ($matches[3])
                    if ($statusFileContent -match '^(red|yellow|green|clear)(?:\+([^ ]+))? ([\s\S]+)$')
                    {
                        $groupColour = $matches[1]
                        $lifeSpan = $matches[2]
                        $statusMessage = $matches[3]

                        $msg = 'status'
                        if ($lifeSpan -ne $null -and $lifeSpan -ne '')
                        {
                            $msg += "+$lifeSpan"
                        }
                        $msg += (' {0}.{1} {2} {3}' -f $hostName, $testName, $groupColour, $statusMessage)
                        
                        WriteLog "Sending Xymon message for file $($f.Name) - test $($testName), host $($hostName): $msg"
                        XymonSend $msg $script:XymonSettings.serversList
                    }
                    else
                    {
                        WriteLog "External File: $($f.Name) - format not recognised"
                        WriteLog "Contents of file:`n$statusFileContent"
                    }
                    WriteLog "Deleting file $($f.Name)"
                    Remove-Item $f.FullName -Force
                }
                else
                {
                    WriteLog "Invalid filename $($f.Name)"
                }
            }
        }
        else
        {
            WriteLog "No files in $($script:XymonSettings.externaldatalocation), nothing to do"
        }
    }
    else
    {
        WriteLog "External data path $($script:XymonSettings.externaldatalocation) does not exist"
    }
    WriteLog 'XymonProcessExternalData finished'
}

# replicate Linux client behaviour
# include items from 'local' folder in client data, if present
# no validation is done on the file content - it's just included
# in the client data with [local:<filename>] tags
function XymonProcessLocalData
{
    WriteLog 'Executing XymonProcessLocalData'

    if (Test-Path $script:XymonSettings.localdatalocation)
    {
        $files = Get-ChildItem $script:XymonSettings.localdatalocation

        if ($files -ne $null)
        {
            foreach ($f in $files)
            {
                # attempt to open the file with an exclusive lock
                # if we cannot, the file may be being updated by a running job, so
                # we will ignore it until the next poll
                WriteLog "Attempting to process local file $($f.FullName)"

                $statusFileContent = ''

                try
                {
                    $statusFile = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'None')
                    $reader = New-Object System.IO.StreamReader($statusFile)
                    $statusFileContent = $reader.ReadToEnd()
                    $reader.Close()
                    $statusFile.Close()
                }
                catch
                {        
                    # if this file is locked or other errors, skip and go to the next one
                    if ($_ -like '*The process cannot access the file*because it is being used by another process*')
                    {
                        WriteLog "Local file $($f.Name) is locked by another process, skipping"
                    }
                    else
                    {
                        WriteLog "Local file $($f.Name) error accessing file, skipping: $_"
                    }
                    continue
                }

                if ($statusFileContent -ne '')
                {
                    $heading = "[local:$($f.Name)]"
                    $heading
                    $statusFileContent
                }

                WriteLog "Deleting file $($f.Name)"
                Remove-Item $f.FullName -Force
            }
        }
        else
        {
            WriteLog "No files in $($script:XymonSettings.localdatalocation), nothing to do"
        }

    }
    else
    {
        WriteLog "Local data path $($script:XymonSettings.localdatalocation) does not exist, nothing to do"
    }

    WriteLog 'XymonProcessLocalData finished'
}

# from http://poshcode.org/1054
function Remove-Diacritics([string]$String) 
{
    $objD = $String.Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    for ($i = 0; $i -lt $objD.Length; $i++) 
    {
        $c = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($objD[$i])
        if($c -ne [Globalization.UnicodeCategory]::NonSpacingMark) 
        {
            [void]$sb.Append($objD[$i])
        }
    }
    return("$sb".Normalize([Text.NormalizationForm]::FormC))
}

function DecryptHttpServerPassword
{
    $serverPassword = $script:XymonSettings.serverHttpPassword
    if ($serverPassword -like '{SecureString}*')
    {
        WriteLog '  Decrypting serverHttpPassword'
        $serverPass = ($serverPassword -replace '^{SecureString}', '')
        try
        {
            $securePass = ConvertTo-SecureString -String $serverPass
            $tempCred = New-Object System.Management.Automation.PSCredential 'N/A', $securePass
            $serverPassword = $tempCred.GetNetworkCredential().Password
        }
        catch
        {
            WriteLog "Failed to decrypt serverHttpPassword: $_"
            $serverPassword = ''
        }
    }
    return $serverPassword
}

function XymonSendViaHttp($msg)
{
    WriteLog 'Executing XymonSendViaHttp'

    $url = $script:XymonSettings.serverUrl
    if ($url -notmatch '^https?://')
    {
        WriteLog "  ERROR: invalid server Url, check config: $url"
        return ''
    }

    WriteLog "  Using url $url"
    $encodedAuth = ''
    if ($script:XymonSettings.serverHttpUsername -ne '')
    {
        $serverHttpPassword = DecryptHttpServerPassword
        $authString = ('{0}:{1}' -f $script:XymonSettings.serverHttpUsername, `
            $serverHttpPassword)
        
        $encodedAuth = [System.Convert]::ToBase64String(`
            [System.Text.Encoding]::GetEncoding('ISO-8859-1').GetBytes($authString))


        WriteLog "  Using username $($script:XymonSettings.serverHttpUsername)"
    }

    # no Invoke-RestMethod before Powershell 3.0
    $request = [System.Net.HttpWebRequest]::Create($url)
    $request.Method = 'POST'
    $request.Timeout = $script:XymonSettings.serverHttpTimeoutMs
    if ($encodedAuth -ne '')
    {
        $request.Headers.Add('Authorization', "Basic $encodedAuth")
    }

    $body = [byte[]][char[]]$msg
    $bodyStream = $request.GetRequestStream()
    $bodyStream.Write($body, 0, $body.Length)

    WriteLog "  Connecting to $($url), body length $($body.Length), timeout $($script:XymonSettings.serverHttpTimeoutMs)ms"
    try
    {
        $response = $request.GetResponse()
    }
    catch
    {
        WriteLog "  Exception connecting to $($url):`n$($_)"
        return ''
    }
        
    $statusCode = [int]($response.StatusCode)
    if ($response.StatusCode -ne [System.Net.HttpStatusCode]::OK)
    {
        WriteLog "  FAILED, HTTP response code: $($response.StatusCode) ($statusCode)"
        return ''
    }

    $responseStream = $response.GetResponseStream()
    $readStream = New-Object System.IO.StreamReader $responseStream
    $output = $readStream.ReadToEnd()
    WriteLog "  Received $($output.Length) bytes from server"
    $script:LastTransmissionMethod = 'HTTP'

    WriteLog 'XymonSendViaHttp finished'
    return $output
}

function XymonSend($msg, $servers, $filePath)
{
    $saveresponse = 1   # Only on the first server
    $outputbuffer = ""

    if ($script:XymonSettings.serverUrl -ne '')
    {
        $outputBuffer = XymonSendViaHttp $msg
    }
    else
    {
        if ($script:XymonSettings.XymonAcceptUTF8 -eq 1) 
        {
            WriteLog 'Using UTF8 encoding'
            $MessageEncoder = New-Object System.Text.UTF8Encoding
        }
        else 
        {
            WriteLog 'Using ASCII encoding'
            $MessageEncoder = New-Object System.Text.ASCIIEncoding
            # remove diacritics
            $msg = Remove-Diacritics -String $msg
            # convert non-break spaces to normal spaces
            $msg = $msg.Replace([char]0x00a0,' ')
        }

        foreach ($srv in $servers) 
        {
            $srvparams = $srv.Split(":")
            # allow for server names that may resolve to multiple A records
            $srvIPs = & {
                $local:ErrorActionPreference = "SilentlyContinue"
                $srvparams[0] | %{[system.net.dns]::GetHostAddresses($_)} | %{ $_.IPAddressToString}
            }
            if ($srvIPs -eq $null) 
            { # no IP addresses could be looked up
                Write-Error -Category InvalidData ("No IP addresses could be found for host: " + $srvparams[0])
            } 
            else 
            {
                if ($srvparams.Count -gt 1) 
                {
                    $srvport = $srvparams[1]
                } 
                else 
                {
                    $srvport = 1984
                }
                foreach ($srvip in $srvIPs) 
                {
                    WriteLog "Connecting to host $srvip"

                    $saveerractpref = $ErrorActionPreference
                    $ErrorActionPreference = "SilentlyContinue"
                    $socket = new-object System.Net.Sockets.TcpClient
                    $socket.Connect($srvip, $srvport)
                    $ErrorActionPreference = $saveerractpref
                    if(! $? -or ! $socket.Connected ) 
                    {
                        $errmsg = $Error[0].Exception
                        WriteLog "ERROR: Cannot connect to host $srv ($srvip) : $errmsg"
                        Write-Error -Category OpenError "Cannot connect to host $srv ($srvip) : $errmsg"
                        continue;
                    }
                    $socket.sendTimeout = 500
                    $socket.NoDelay = $true

                    $stream = $socket.GetStream()
                    
                    $sent = 0
                    foreach ($line in $msg) 
                    {
                        # Convert data as appropriate
                        try
                        {
                            $sent += $socket.Client.Send($MessageEncoder.GetBytes($line.Replace("`r","") + "`n"))
                        }
                        catch
                        {
                            WriteLog "ERROR: $_"
                        }
                    }
                    WriteLog "Sent $sent bytes to server"

                    if ($saveresponse-- -gt 0) 
                    {
                        $socket.Client.Shutdown(1)  # Signal to Xymon we're done writing.

                        $bytes = 0
                        $line = ($msg -split [environment]::newline)[0]
                        $line = $line -replace '[\t|\s]+', ' '
                        if  ($line -match '(download) (.*$)' ) 
                        {
                            if ($filePath -eq $null -or $filePath -eq "") 
                            {
                                # save it locally with the same name
                                $filePath = split-path -leaf $matches[2]
                            }
                            $buffer = new-object System.Byte[] 2048;
                            $fileStream = New-Object System.IO.FileStream($filePath, [System.IO.FileMode]'Create', [System.IO.FileAccess]'Write');

                            do
                            {
                                $read = $null;
                                while($stream.DataAvailable -or $read -eq $null) 
                                {
                                    $read = $stream.Read($buffer, 0, 2048);
                                    if ($read -gt 0) 
                                    {
                                        $fileStream.Write($buffer, 0, $read);
                                        $bytes += $read
                                    }
                                }
                            } while ($read -gt 0);
                            $fileStream.Close();
                            WriteLog "Wrote $bytes bytes from server to $filePath"
                        } 
                        else 
                        {
                            $s = new-object system.io.StreamReader($stream,"ASCII")

                            start-sleep -m 200  # wait for data to buffer
                            try
                            {
                                $outputBuffer = $s.ReadToEnd()
                                WriteLog "Received $($outputBuffer.Length) bytes from server"
                            }
                            catch
                            {
                                WriteLog "ERROR: $_"
                            }
                        }
                    } # saveresponse-- -gt 0
                    $socket.Close()
                    $script:LastTransmissionMethod = 'TCP'
                } # foreach ($srvip in $srvIPs)
            } # else of if ($srvIPs -eq $null) 
        } # foreach $srv in $servers
    }
    $outputbuffer
}

function XymonClientConfig($cfglines)
{
    if ($cfglines -eq $null -or $cfglines -eq "") { return }

    # Convert to Windows-style linebreaks
    $script:clientlocalcfg = $cfglines.Split("`n")

    # overwrite local cached config with this version if 
    # remote config is enabled
    if ($script:XymonSettings.clientremotecfgexec -ne 0)
    {
        WriteLog "Using new remote config, saving locally"
        $clientlocalcfg >$script:XymonSettings.clientconfigfile
    }
    else
    {
        WriteLog "Using local config only (if one exists), clientremotecfgexec = 0"
    }

    # Parse the config - always uses the local file (which may contain
    # config from remote)
    if (test-path -PathType Leaf $script:XymonSettings.clientconfigfile) 
    {
        $script:clientlocalcfg_entries = @{}
        $lines = get-content $script:XymonSettings.clientconfigfile
        $currentsection = ''
        foreach ($l in $lines)
        {
            # change this to recognise new config items
            if ($l -match '^eventlog:' -or $l -match '^servicecheck:' `
                -or $l -match '^dir:' -or $l -match '^file:' `
                -or $l -match '^dirsize:' -or $l -match '^dirtime:' `
                -or $l -match '^log' -or $l -match '^clientversion:' `
                -or $l -match '^eventlogswanted' `
                -or $l -match '^servergifs:' `
                -or $l -match '^(?:ts|terminalservices)sessions:' `
                -or $l -match '^adreplicationcheck' `
                -or $l -match '^ifstat:' `
                -or $l -match '^repeattest:' `
                -or $l -match '^proc(?:ess)?runtime:' `
                -or $l -match '^external:' `
                -or $l -match '^xymonlogsend' `
                -or $l -match '^slimmode' `
                )
            {
                WriteLog "Found a command: $l"
                $currentsection = $l
                $script:clientlocalcfg_entries[$currentsection] = @()
            }
            elseif ($l -ne '')
            {
                $script:clientlocalcfg_entries[$currentsection] += $l
            }
        }

        # re-parse slimmode config to make it easier
        if ($script:clientlocalcfg_entries.ContainsKey('slimmode'))
        {
            $slimConfig = @{}
            $script:clientlocalcfg_entries.slimmode | `
               foreach { $i = ($_ -split ':'); $slimConfig[$i[0]] = $i[1] }

            $script:clientlocalcfg_entries.slimmode = $slimConfig

            ('services', 'processes') | foreach `
            {
                if ($script:clientlocalcfg_entries.slimmode.ContainsKey($_))
                {
                    $script:clientlocalcfg_entries.slimmode.$_ = `
                        ($script:clientlocalcfg_entries.slimmode.$_ -split ',')
                }
            }
        }
    }
    WriteLog "Cached config now contains: "
    WriteLog ($script:clientlocalcfg_entries.keys -join ', ')

    # special handling for servergifs
    $gifpath = @($script:clientlocalcfg_entries.keys | where { $_ -match '^servergifs:(.+)$' })
    if ($gifpath.length -eq 1)
    {
        $script:XymonSettings.servergiflocation = $matches[1]
    }
}

function XymonReportConfig
{
    # exclude serverHttpPassword from output
    $settings = (($script:XymonSettings | Out-String) -split [System.Environment]::NewLine) | `
        where { $_ -notmatch '^serverHttpPassword' }

    "[XymonConfig]"
    "XymonSettings"
    $settings
    ""
    "HaveCmd"
    $HaveCmd
    foreach($v in @("XymonClientVersion", "clientname" )) {
        ""; "$v"
        (Get-Variable $v).Value
    }
    "[XymonPSClientInfo]"
    "Collection number: $($script:collectionnumber)"
    "Last transmission method: $($script:LastTransmissionMethod)"
    $script:thisXymonProcess    

    #get-process -id $PID
    #"[XymonPSClientThreadStats]"
    #(get-process -id $PID).Threads
}

function XymonClientSections([boolean] $isSlowScan)
{
    # maybe move XymonManageExternals to slow scan tasks
    XymonManageExternals
    XymonExecuteExternals $isSlowScan

    XymonClientVersion
    XymonUname
    XymonCpu
    XymonDisk
    XymonMemory
    XymonEventLogs
    XymonMsgs
    XymonProcs

    $includeSections = @('Netstat', 'Ports', 'IPConfig', 'Route', 'Ifstat', 'Who', 'Users')
    if ($script:clientlocalcfg_entries.ContainsKey('slimmode'))
    {
        $includeSections = @()
        if ($script:clientlocalcfg_entries.slimmode.ContainsKey('sections'))
        {
            $includeSections += $script:clientlocalcfg_entries.slimmode.sections
        }
    }

    if ($includeSections -contains 'Netstat') { XymonNetstat }
    if ($includeSections -contains 'Ports') { XymonPorts }
    if ($includeSections -contains 'IPConfig') { XymonIPConfig }
    if ($includeSections -contains 'Route') { XymonRoute }
    if ($includeSections -contains 'Ifstat') { XymonIfstat }

    XymonSvcs
    XymonDir
    XymonFileCheck
    XymonLogCheck
    XymonUptime
    if ($includeSections -contains 'Who') { XymonWho }
    if ($includeSections -contains 'Users') { XymonUsers }

    if ($script:XymonSettings.EnableWMISections -eq 1)
    {
        XymonWMIOperatingSystem
        XymonWMIComputerSystem
        XymonWMIBIOS
        XymonWMIProcessor
        XymonWMIMemory
        XymonWMILogicalDisk
    }

    XymonServiceCheck
    XymonDirSize
    XymonDirTime
    XymonTerminalServicesSessionsCheck
    XymonActiveDirectoryReplicationCheck
    XymonProcessRuntimeCheck
    XymonProcessExternalData
    XymonProcessLocalData

    $XymonIISSitesCache
    $XymonWMIQuickFixEngineeringCache
    $XymonWMIProductCache

    XymonReportConfig
}

function XymonClientInstall([string]$scriptname)
{
    # client install re-written to use NSSM
    # also to remove any existing service first
    
    XymonClientUnInstall

    & "$xymondir\nssm.exe" install `"$xymonsvcname`" `"$PSHOME\powershell.exe`" -ExecutionPolicy RemoteSigned -NoLogo -NonInteractive -NoProfile -WindowStyle Hidden -File `"`"`"$scriptname`"`"`"
    # "
}

function XymonClientUnInstall()
{
    if ((Get-Service -ea:SilentlyContinue $xymonsvcname) -ne $null)
    {
        Stop-Service $xymonsvcname
        $service = Get-WmiObject -Class Win32_Service -Filter "Name='$xymonsvcname'"
        $service.delete() | out-null

        Remove-Item -Path HKLM:\SYSTEM\CurrentControlSet\Services\$xymonsvcname\* -Recurse -ErrorAction SilentlyContinue
    }
}

function ExecuteSelfUpdate([string]$newversion)
{
    $oldversion = $MyInvocation.ScriptName

    WriteLog "Upgrading $oldversion to $newversion"

    # copy newversion to correct name
    # remove newversion file
    # re-start service - by exiting, NSSM will notice the process has ended and will
    # automatically restart it

    copy-item "$newversion" "$oldversion" -force
    remove-item "$newversion"
    WriteLog "Restarting service..."
    exit
}

function XymonDownloadFromFile([string]$downloadPath, [string]$destinationFilePath)
{
    WriteLog "XymonDownloadFromFile - Downloading $downloadPath to $destinationFilePath"
    if (!(Test-Path $downloadPath))
    {
        WriteLog "File $downloadPath cannot be found - aborting"
        return $false
    }

    WriteLog "Copying $downloadPath to $destinationPath"
    try
    {
        Copy-Item  $downloadPath $destinationFilePath -Force
    }
    catch 
    {
        WriteLog "Error copying file: $_"
        return $false
    }
    return $true
}

function XymonDownloadFromURL([string]$downloadURL, [string]$destinationFilePath)
{
    $downloadURL = $downloadURL.Trim()
    WriteLog "XymonDownloadFromURL - Downloading $downloadURL to $destinationFilePath"
    $client = New-Object System.Net.WebClient
    try
    {
        # for self-signed certificates, turn off cert validation
        # TODO: make this a config option
        [Net.ServicePointManager]::ServerCertificateValidationCallback = {$true}
        $client.DownloadFile($downloadURL, $destinationFilePath)
    }
    catch
    {
        WriteLog "Error downloading: $_"
        return $false
    }
    return $true
}

function XymonDownloadFromServer([string]$ServerPath, [string]$destinationFilePath)
{
    $ServerPath = $ServerPath.Trim()
    WriteLog "XymonDownloadFromServer - Downloading $ServerPath to $destinationFilePath"
    $message = "download $ServerPath"
    try
    {
        # should work transparently through any intermediate proxies
        XymonSend $message $script:XymonSettings.serversList $destinationFilePath
    }
    catch
    {
        WriteLog "Error downloading: $_"
        return $false
    }
    return $true
}

function GetHashValueForFile([string] $filename, [string] $hashAlgorithm)
{
    $hash = [System.Security.Cryptography.HashAlgorithm]::Create($hashAlgorithm)
    $stream = ([System.IO.StreamReader]$filename).BaseStream
    $fileHash = -join ($hash.ComputeHash($stream) | foreach { '{0:x2}' -f $_ } )
    $stream.Close()
    return $fileHash
}

function XymonCheckUpdate
{
    WriteLog "Executing XymonCheckUpdate"
    $updates = @($script:clientlocalcfg_entries.keys | `
        where { $_ -match '^clientversion:(\d+\.\d+):(.+?)(?::(MD5|SHA1|SHA256):([0-9a-f]+))?$' })
    if ($updates.length -gt 1)
    {
        WriteLog "ERROR: more than one clientversion directive in config!"
    }
    elseif ($updates.length -eq 1)
    {
        # $matches[1] = the new version number
        # $matches[2] = the place to look for new version file
        # $matches[3] = (optional) hash type
        # $matches[4] = (optional) hash value

        if ($Version -lt $matches[1])
        {
            WriteLog "Running version $Version; config version $($matches[1]); attempting upgrade"

            # $matches[2] can be either a http[s] URL, bb fake URL or a file path
            $updatePath = $matches[2]
            $updateFile = "xymonclient_$($matches[1]).ps1"
            $hashAlgorithm = $matches[3]
            $hashRequired = $matches[4]
            $destination = Join-Path -Path $xymondir -ChildPath $updateFile

            $result = $false
            if ($updatePath -match '^http')
            {
                $updateURL = $updatePath.Trim()
                if ($updateURL -notmatch '/$')
                {
                    $updateURL += '/'
                }
                $URL = "{0}{1}" -f $updateURL, $updateFile
                $destination = Join-Path -Path $xymondir -ChildPath $updateFile
                $result = XymonDownloadFromURL $URL $destination
            }
            elseif ($updatePath -match '^bb')
            {
                $ServerPath = $updatePath.Trim()
                $ServerPath = $ServerPath -creplace '^[^:]*:/*',''
                if ($ServerPath -notmatch '/$')
                {
                    $ServerPath += '/'
                }
                $URL = "{0}{1}" -f $ServerPath, $updateFile
                $destination = Join-Path -Path $xymondir -ChildPath $updateFile
                $result = XymonDownloadFromServer $URL $destination
            }
            else
            {
                $updateSource = Join-Path $updatePath $updateFile
                $result = XymonDownloadFromFile $updateSource $destination
            }

            if ($result)
            {
                $newversion = Join-Path $xymondir $updateFile
                if ($hashAlgorithm -ne $null)
                {
                    WriteLog "$($hashAlgorithm) hash specified, testing update file"
                    $fileHash = ''
                    try
                    {
                        $fileHash = GetHashValueForFile -filename $newversion -hashAlgorithm $hashAlgorithm
                    }
                    catch
                    {
                        WriteLog "Update directive specifies hash, but error calculating hash: $_"
                        WriteLog "Update cancelled"
                        Remove-Item $newversion
                        return
                    }

                    if ($fileHash -ne $hashRequired)
                    {
                        WriteLog "Update: update file hash mismatch (calculated $fileHash should be $hashRequired)"
                        WriteLog "Update cancelled"
                        Remove-Item $newversion
                        return
                    }
                    else
                    {
                        WriteLog "Update file hash matches expected value, update can proceed"
                    }
                }

                WriteLog "Launching update"
                ExecuteSelfUpdate $newversion
            }
        }
        else
        {
            WriteLog "Update: Running version $Version; config version $($matches[1]); doing nothing"
        }
    }
    else
    {
        # no clientversion directive
        WriteLog "Update: No clientversion directive in config, nothing to do"
    }
}

function DownloadAndVerify([string] $URI, [string] $name, [string] $path, `
    [string] $hashAlgorithm, [string] $hashRequired)
{
    if (!(Test-Path $path))
    {
        New-Item -ItemType directory -Path $path
    }

    $tempName = "$($name)_new"
    $destination = Join-Path -Path $path -ChildPath $tempName

    $result = $false
    if ($URI -match '^http')
    {
        $result = XymonDownloadFromURL $URI $destination
    }
    elseif ($URI -match '^bb')
    {
        $URI = $URI -creplace '^[^:]*:/*',''
        $result = XymonDownloadFromServer $URI $destination
    }
    else
    {
        $result = XymonDownloadFromFile $URI $destination
    }

    if ($result -and $hashAlgorithm -ne $null)
    {
        WriteLog "$($hashAlgorithm) hash specified, testing destination file"
        $fileHash = ''
        try
        {
            $fileHash = GetHashValueForFile -filename $destination -hashAlgorithm $hashAlgorithm
        }
        catch
        {
            WriteLog "Error calculating hash: $_"
            $result = $false
        }

        if ($result)
        {
            if ($fileHash -ne $hashRequired)
            {
                $result = $false
                WriteLog "File hash mismatch (calculated $fileHash should be $hashRequired)"
            }
            else
            {
                WriteLog "Downloaded file hash matches expected value, can proceed"
            }
        }
        if (!$result)
        {
            WriteLog "Removing failed download $destination"
            Remove-Item $destination
        }
    }
    if ($result)
    {
        $originalFile = Join-Path -Path $path -ChildPath $name
        if (Test-Path $originalFile)
        {
            WriteLog "Deleting original file $originalFile"
            Remove-Item -Force $originalFile
        }
        WriteLog "Renaming $destination to $originalFile"
        Move-Item -Force $destination $originalFile
    }
    return $result
}

function XymonManageExternals
{
    WriteLog "Executing XymonManageExternals"
    $externalConfig = @($script:clientlocalcfg_entries.keys | `
        where { $_ -match '^external:' })
    $script:externals = @()

    foreach ($external in $externalConfig)
    {
        if ($external -match '^external:(?:(\d+):)?(slowscan|everyscan):(sync|async):(.+?)(?:\|(MD5|SHA1|SHA256)\|([0-9a-f]+))?(?:\|(.+)\|(.+))?$')
        {
            # $matches[1] = priority (optional) 0-99
            # $matches[2] = slowscan/everyscan
            # $matches[3] = sync/async
            # $matches[4] = URL / file location
            # $matches[5] = optional hash type
            # $matches[6] = optional hash value
            # $matches[7] = optional process
            # $matches[8] = optional arguments

            ($priority, $executionFrequency, $executionMethod, $externalURI, `
             $hashAlgorithm, $hashRequired, $process, $arguments) = $matches[1..8]

            if ($externalURI -match '^(http|bb)')
            {
                $externalScriptName = $externalURI.SubString($externalURI.LastIndexOf('/') + 1)
            }
            else
            {
                $externalScriptName = Split-Path -Leaf $externalURI
            }
            $externalFullName = Join-Path $script:XymonSettings.externalscriptlocation $externalScriptName
            if ($arguments -ne $null)
            {
                $arguments = $arguments -replace '{script}', $externalFullName
                $arguments = $arguments -replace '{scriptdir}', $script:XymonSettings.externalscriptlocation
            }
            if ($priority -eq $null)
            {
                $priority = 99
            }
            if ($process -eq $null)
            {
                $process = $externalFullName
            }
            $externalInfo = @{ Fullname = $externalFullName; `
                ExecutionFrequency = $executionFrequency; `
                ExecutionMethod = $executionMethod; 
                ProcessName = $process; 
                Arguments = $arguments;
                Priority = $priority }
            $externalObj = New-Object -Type PSObject -Property $externalInfo
            $downloadFlag = $false

            WriteLog "Checking $externalFullName"

            # check to see if we have the matching version
            if (Test-Path $externalFullName)
            {
                WriteLog "External script $externalScriptName found"
                if ($hashAlgorithm -ne $null -and $hashRequired -ne $null)
                {
                    WriteLog "External script $externalScriptName - testing against hash"
                    try
                    {
                        $fileHash = GetHashValueForFile -filename $externalFullName -hashAlgorithm $hashAlgorithm
                    }
                    catch
                    {
                        WriteLog "Error calculating hash for external: $_"
                    }
                    if ($fileHash -ne $hashRequired)
                    {
                        WriteLog "Existing script hash mismatch (calculated $fileHash should be $hashRequired)"
                        # hash mismatch, need to update via download 
                        $downloadFlag = $true
                    }
                }
                if (!$downloadFlag)
                {
                    WriteLog "Success, adding/updating external $externalScriptName in execution plan"
                    $script:externals += $externalObj
                }
            }
            else
            {
                WriteLog "External $externalFullName not found"
                # external does not exist, need to download
                $downloadFlag = $true
            }

            if ($downloadFlag)
            {
                WriteLog "External script $externalScriptName not found or requires update, downloading"
                try
                {
                    $result = DownloadAndVerify -URI $externalURI -name $externalScriptName `
                        -path $script:XymonSettings.externalscriptlocation `
                        -hashAlgorithm $hashAlgorithm -hashRequired $hashRequired
                    
                    if ($result)
                    {
                        WriteLog "Success, adding/updating external $externalScriptName in execution plan"
                        $script:externals += $externalObj
                    }
                }
                catch
                {
                    WriteLog "Error downloading $externalScriptName, ignoring (will not be executed)"
                    WriteLog "Error was: $_"
                }
            }
        }
        else
        {
            WriteLog "external directive does not match expected format: $external"
        }
    } # foreach ... externals
    WriteLog 'XymonManageExternals finished'
}

function XymonExecuteExternals([boolean] $isSlowscan)
{
    WriteLog 'Executing XymonExecuteExternals'
    if (!(Test-Path $script:XymonSettings.externaldatalocation))
    {
        New-Item -ItemType directory -Path $script:XymonSettings.externaldatalocation
    }
    $script:externals | Sort-Object Priority, ExecutionMethod | foreach {
        WriteLog "External: $($_.ExecutionFrequency) - $($_.FullName)"
        if (!$isSlowscan -and $_.ExecutionFrequency -eq 'slowscan')
        {
            WriteLog 'Skipping execution, this is not a slow scan'
        }
        else
        {
            try
            {
                $process = $_.ProcessName
                $arguments = $_.Arguments
                if ($arguments -ne $null)
                {
                    WriteLog "Executing $process with arguments $arguments"
                    $extpid = Start-Process -PassThru `
                        -WindowStyle Hidden `
                        -WorkingDirectory $script:XymonSettings.externalscriptlocation `
                        $process $arguments
                }
                else
                {
                    WriteLog "Executing $process with no arguments"
                    $extpid = Start-Process -PassThru `
                        -WindowStyle Hidden `
                        -WorkingDirectory $script:XymonSettings.externalscriptlocation `
                        $process
                }
                WriteLog "Process $($extpid.Id) started"
            
                if ($_.ExecutionMethod -eq 'sync')
                {
                    WriteLog "Synchronous external: waiting for process $($extpid.Id) to complete"
                    $extpid | Wait-Process
                    WriteLog "Process $($extpid.Id) completed"
                }
                else
                {
                    WriteLog "Asynchronous: not waiting for process $($extpid.Id)"
                }
            }
            catch
            {
                WriteLog "Error executing: $_"
            }
        }
    }
    WriteLog 'XymonExecuteExternals finished'
}

function WriteLog([string]$message)
{
    $datestamp = get-date -uformat '%Y-%m-%d %H:%M:%S'
    add-content -Path $script:XymonSettings.clientlogfile -Value "$datestamp  $message"
    Write-Host "$datestamp  $message"
}

function RotateLog([string]$logfile)
{
    $retain = $script:XymonSettings.clientlogretain
    if ($retain -gt 99)
    {
        $retain = 99
    }
    if ($retain -gt 0)
    {
        WriteLog "Rotating logfile $logfile"
        if (Test-Path $logfile)
        {
            $lastext = "{0:00}" -f $retain
            if (Test-Path "$logfile.$lastext")
            {
                WriteLog "Removing $logfile.$lastext"
                Remove-Item -Force "$logfile.$lastext"
            }

            (($retain - 1) .. 1) | foreach {
                # pad 1 -> 01 etc
                $ext = "{0:00}" -f $_
                if (Test-Path "$logfile.$ext")
                {
                    # pad 1 -> 01, 2 -> 02 etc
                    $newext = "{0:00}" -f ($_ + 1)
                    WriteLog "Renaming $logfile.$ext to $logfile.$newext"
                    Move-Item -Force "$logfile.$ext" "$logfile.$newext"
                }
            }

            if (Test-Path $logfile)
            {
                WriteLog "Finally: Renaming $logfile to $logfile.01"
                Move-Item -Force $logfile "$logfile.01"
            }
        }
    }
}

function RepeatTests([string] $content)
{
    if (@($script:clientlocalcfg_entries.Keys -like 'repeattest*').Length -eq 0)
    {
        WriteLog "RepeatTests: nothing to do!"
        return
    }

    WriteLog 'Executing RepeatTests'

    $lines = $content -split [environment]::newline
    $capturelines = $false
    $capturedSection = ''

    foreach ($line in $lines)
    {
        if ($line -match '^\[([^\]]+)\]$')
        {
            $currentSection = $matches[1]
            # found a new section - if we were previously capturing lines from the 
            # previous section, write out any repeat sections and reset
            if ($capturelines)
            {
                $capturelines = $false
                # we were capturing lines - check for alerts and send to Xymon
                $regex = "^repeattest:$($capturedSection):(.+)"
                $script:clientlocalcfg_entries.keys | where { $_ -match $regex } | foreach {
                    $newsection = $matches[1]
                    $outputHeader = @()
                    $outputHeader += (get-date -format G) + "<br><h2>$newsection</h2>"                
                    $groupcolour = 'green'
                    # check for triggers
                    if ($script:clientlocalcfg_entries[$_] -ne $null)
                    {
                        foreach ($trigger in $script:clientlocalcfg_entries[$_])
                        {
                            $alertcolour = 'green'
                            $alertLines = @()
                            if ($trigger -match '^trigger:([a-z]+):(.+)$')
                            {
                                $triggerAlertcolour = $Matches[1]
                                $triggerRegex = $Matches[2]
                                foreach ($line in $capturedlines)
                                {
                                    if ($line -match $triggerRegex)
                                    {
                                        $alertcolour = $triggerAlertcolour
                                        $alertLines += "matches `"$line`""
                                    }
                                }
                                if ($alertLines.Length -eq 0)
                                {
                                    $alertLine = 'no match'
                                }
                                else
                                {
                                    $alertLine = $alertLines -join '<br>'
                                }
                                $outputHeader += ('<img src="{3}{0}.gif" alt="{0}" height="16" width="16" border="0"> {1} {2}<br>' `
                                    -f $alertcolour, $trigger, $alertLine, $script:XymonSettings.servergiflocation)
                                if ($groupcolour -eq 'green' -and $alertcolour -eq 'yellow')
                                {
                                    $groupcolour = 'yellow'
                                }
                                elseif ($alertcolour -eq 'red')
                                {
                                    $groupcolour = 'red'
                                }
                            }
                        }
                    }

                    $outputHeader += '<br>'
                    $output = ($outputHeader -join "`n")
                    $output += ($capturedlines -join '<br>')
                    # repeat the test by sending to Xymon
                    WriteLog "Sending repeated test: $newsection"
                    $outputXymon = ('status {0}.{1} {2} {3}' -f $script:clientname, $newsection, $groupcolour, $output)
                    XymonSend $outputXymon $script:XymonSettings.serversList
                }
            }
            $capturedlines = @()
            $capturedSection = $currentSection -replace '\\', '\\'
            $regex = "^repeattest:$($capturedSection):(.+)"
            # check to see if the new section is one we want to repeat
            $script:clientlocalcfg_entries.keys | where { $_ -match $regex } | foreach {
                $capturelines = $true
            }
        }
        elseif ($capturelines)
        {
            $capturedlines += $line
        }
    }
    WriteLog 'RepeatTests finished'
}

function XymonLogSend()
{
    if (@($script:clientlocalcfg_entries.Keys -eq 'xymonlogsend').Length -eq 0)
    {
        WriteLog 'XymonLogSend: nothing to do!'
        return
    }

    WriteLog 'XymonLogSend - sending log'

    $log = ((get-content $script:XymonSettings.clientlogfile) -join "`n")
    $log = [System.Web.HttpUtility]::HtmlEncode($log)

    $output = (get-date -format G) + '<br><h2>Xymon client log</h2><pre>' 
    $output += $log
    $output += '</pre>'

    $outputXymon = ('status {0}.{1} {2} {3}' -f $script:clientname, 'xymonlog', 'green', $output)
    XymonSend $outputXymon $script:XymonSettings.serversList

    WriteLog 'XymonLogSend - finished'
}

##### Main code #####
$script:thisXymonProcess = get-process -id $PID
$script:thisXymonProcess.PriorityClass = "High"
$hasargs = $false
if ($args -ne $null)
{
    $hasargs = $true
}
XymonConfig $hasargs
$ret = 0
# check for install/set/unset/config/start/stop for service management
if($args -eq "Install") {
    XymonClientInstall $MyInvocation.MyCommand.Definition
    $ret=1
}
if ($args -eq "uninstall")
{
    XymonClientUnInstall
    $ret=1
}
if($args[0] -eq "config") {
    "XymonPSClient config:`n"
    $XymonCfgLocation
    "Settable Params and values:"
    foreach($param in $script:XymonSettings | gm -memberType NoteProperty,Property) {
        if($param.Name -notlike "PS*") {
            $val = $script:XymonSettings.($param.Name)
            if($val -is [Array]) {
                $out = [string]::join(" ",$val)
            } else {
                $out = $val.ToString()
            }
            "    {0}={1}" -f $param.Name,$out
        }
    }
    return
}
if($args -eq "Start") {
    if((get-service $xymonsvcname).Status -ne "Running") { start-service $xymonsvcname }
    return
}
if($args -eq "Stop") {
    if((get-service $xymonsvcname).Status -eq "Running") { stop-service $xymonsvcname }
    return
}
if($ret) {return}
if($args -ne $null) {
    "Usage: "+ $MyInvocation.MyCommand.Definition +" install | uninstall | start | stop | config "
    return
}

# assume no other args, so run as normal

# elevate our priority to configured setting
$script:thisXymonProcess.PriorityClass = $script:XymonSettings.ClientProcessPriority

# ZB: read any cached client config
if (Test-Path -PathType Leaf $script:XymonSettings.clientconfigfile)
{
    $cfglines = (get-content $script:XymonSettings.clientconfigfile) -join "`n"
    XymonClientConfig $cfglines
}

$lastcollectfile = join-path $script:XymonSettings.clientlogpath 'xymon-lastcollect.txt'
$running = $true
$script:collectionnumber = (0 -as [long])
$loopcount = ($script:XymonSettings.slowscanrate - 1)

AddHelperTypes

while ($running -eq $true) {
    # log file setup/maintenance
    RotateLog $lastcollectfile
    RotateLog $script:XymonSettings.clientlogfile
    Set-Content -Path $script:XymonSettings.clientlogfile `
        -Value "$clientname - $XymonClientVersion"

    $script:collectionnumber++
    $loopcount++ 
    $UTCstr = get-date -Date ((get-date).ToUniversalTime()) -uformat '%Y-%m-%d %H:%M:%S'
    WriteLog "UTC date/time: $UTCstr"
    WriteLog "This is collection number $($script:collectionnumber), loop count $loopcount"
    WriteLog "Next 'slow scan' is when loopcount reaches $($script:XymonSettings.slowscanrate)"

    $starttime = Get-Date
    $slowscan = $false
    
    if ($loopcount -eq $script:XymonSettings.slowscanrate) { 
        $loopcount = 0
        $slowscan = $true
        
        WriteLog "Doing slow scan tasks"

        WriteLog "Executing XymonWMIQuickFixEngineering"
        $XymonWMIQuickFixEngineeringCache = XymonWMIQuickFixEngineering
        WriteLog "Executing XymonWMIProduct"
        $XymonWMIProductCache = XymonWMIProduct
        WriteLog "Executing XymonIISSites"
        $XymonIISSitesCache = XymonIISSites
        $script:diskpartData = XymonDiskPart

        WriteLog "Slow scan tasks completed."
    }

    XymonCollectInfo
    
    WriteLog "Performing main and optional tests and building output..."
    $clout = "client $($clientname).$($script:XymonSettings.clientsoftware) $($script:XymonSettings.clientclass) XymonPS" | 
        Out-String
    $clsecs = XymonClientSections $slowscan | Out-String
    $localdatetime = Get-Date
    $clout += XymonDate | Out-String
    $clout += XymonClock | Out-String
    $clout +=  $clsecs
    
    #XymonReportConfig >> $script:XymonSettings.clientlogfile
    WriteLog "Main and optional tests finished."
    
    WriteLog "Sending to server"
    Set-Content -path $lastcollectfile -value $clout
        
    $newconfig = XymonSend $clout $script:XymonSettings.serversList

    RepeatTests $clout

    XymonClientConfig $newconfig
    [GC]::Collect() # run every time to avoid memory bloat
    
    #maybe check for update - only happens after a slow scan, when loopcount = 0
    if ($slowscan)
    {
        XymonCheckUpdate
    }

    $delay = ($script:XymonSettings.loopinterval - (Get-Date).Subtract($starttime).TotalSeconds)
    if ($script:collectionnumber -eq 1)
    {
        # if this is the very first collection, make the second collection happen sooner
        # than the normal delay - this is because CPU usage is not collected on the 
        # first run
        $delay = 30
    }
    XymonLogSend
    WriteLog "Delaying until next run: $delay seconds"
    if ($delay -gt 0) { sleep $delay }
}
