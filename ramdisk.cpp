#include "ramdisk.h"
#include <QMessageBox>

const QString CRamDisk::driveLetter = "R:";
const QString CRamDisk::driveFileSystem = "/fs:ntfs";
const quint64 CRamDisk::driveSize = 7ull*1024*1024*1024;         // 1Gb * 10^9 = bytes

CRamDisk *CRamDisk::_instance = nullptr;

// Wrapper for ImDisk
CRamDisk::CRamDisk(QObject *parent) : QObject(parent), _wasMounted(false) //-V730
{
    qDebug() << Q_FUNC_INFO;

	_deviceNumber = 0;
}

void CRamDisk::init()
{
    qDebug() << Q_FUNC_INFO;

    _deviceNumber = IMDISK_AUTO_DEVICE_NUMBER;

    ZeroMemory(&_imageOffset, sizeof(LARGE_INTEGER));
    ZeroMemory(&_diskGeometry, sizeof(DISK_GEOMETRY));

    _diskGeometry.Cylinders.QuadPart = driveSize;
}

CRamDisk *CRamDisk::getInstance()
{
    if(!_instance)
        _instance = new CRamDisk;
    return _instance;
}

void CRamDisk::destroyInstance()
{
    qDebug() << Q_FUNC_INFO;

    if(_instance)
    {
        delete _instance;
        _instance = nullptr;
    }
}

CRamDisk::~CRamDisk()
{
    qDebug() << Q_FUNC_INFO;
}

void CRamDisk::mount()
{
    qDebug() << Q_FUNC_INFO;

    QString format = QString("%1 /q /y").arg(driveFileSystem);

    this->ImDiskCliCreateDevice(&_deviceNumber, &_diskGeometry, &_imageOffset, 0, NULL, FALSE,
                                (LPWSTR)driveLetter.toStdWString().c_str(), FALSE, (LPWSTR)format.toStdWString().c_str(), FALSE);
    _wasMounted = true;
}

void CRamDisk::unmount()
{
    qDebug() << Q_FUNC_INFO;

    this->ImDiskCliRemoveDevice(_deviceNumber, driveLetter.toStdWString().c_str(), TRUE, FALSE, FALSE);
    _wasMounted = false;
}

bool CRamDisk::wasMounted()
{
    return _wasMounted;
}


// ============================================
// WinAPI, C-style code, (Hungarian Notation)
// ============================================
BOOL CRamDisk::ImDiskOemPrintF(FILE *Stream, LPCSTR Message, ...)
{
    va_list param_list;
    LPSTR lpBuf = NULL;

    va_start(param_list, Message);

    if (!FormatMessageA(78 |
                        FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_STRING, Message, 0, 0,
                        (LPSTR) &lpBuf, 0, &param_list))
	{
		va_end(param_list);
        return FALSE;
	}

    CharToOemA(lpBuf, lpBuf);
    fprintf(Stream, "%s\n", lpBuf);
    LocalFree(lpBuf);
	va_end(param_list);
    return TRUE;
}

VOID CRamDisk::PrintLastError(LPCWSTR Prefix)
{
    LPSTR MsgBuf;

    if (!FormatMessageA(FORMAT_MESSAGE_MAX_WIDTH_MASK |
                        FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, GetLastError(), 0, (LPSTR) &MsgBuf, 0, NULL))
        MsgBuf = NULL;

    ImDiskOemPrintF(stderr, "%1!ws! %2", Prefix, MsgBuf);

    if (MsgBuf != NULL)
        LocalFree(MsgBuf);
}

LPVOID CRamDisk::ImDiskCliAssertNotNull(LPVOID Ptr)
{
    if (Ptr == NULL)
        RaiseException(STATUS_NO_MEMORY,
                       EXCEPTION_NONCONTINUABLE,
                       0,
                       NULL);

    return Ptr;
}

BOOL CRamDisk::ImDiskCliCheckDriverVersion(HANDLE Device)
{
    DWORD VersionCheck;
    DWORD BytesReturned;

    if (!DeviceIoControl(Device,
                         IOCTL_IMDISK_QUERY_VERSION,
                         NULL, 0,
                         &VersionCheck, sizeof VersionCheck,
                         &BytesReturned, NULL))
        switch (GetLastError())
        {
        case ERROR_INVALID_FUNCTION:
        case ERROR_NOT_SUPPORTED:
            fputs("Error: Not an ImDisk device.\r\n", stderr);
            return FALSE;

        default:
            PrintLastError(L"Error opening device:");
            return FALSE;
        }

    if (BytesReturned < sizeof VersionCheck)
    {
        fprintf(stderr,
                "Wrong version of ImDisk Virtual Disk Driver.\n"
                "No current driver version information, expected: %u.%u.\n"
                "Please reinstall ImDisk and reboot if this issue persists.\n",
                HIBYTE(IMDISK_DRIVER_VERSION), LOBYTE(IMDISK_DRIVER_VERSION));
        return FALSE;
    }

    if (VersionCheck != IMDISK_DRIVER_VERSION)
    {
        fprintf(stderr,
                "Wrong version of ImDisk Virtual Disk Driver.\n"
                "Expected: %u.%u Installed: %u.%u\n"
                "Please re-install ImDisk and reboot if this issue persists.\n",
                HIBYTE(IMDISK_DRIVER_VERSION), LOBYTE(IMDISK_DRIVER_VERSION),
                HIBYTE(VersionCheck), LOBYTE(VersionCheck));
        return FALSE;
    }

    return TRUE;
}

BOOL CRamDisk::ImDiskCliValidateDriveLetterTarget(LPCWSTR DriveLetter, LPCWSTR ValidTargetPath)
{
    DWORD len = (DWORD)wcslen(ValidTargetPath) + 2;
    LPWSTR target = (LPWSTR)ImDiskCliAssertNotNull(_alloca(len << 1));

    if (QueryDosDevice(DriveLetter, target, len))
        if (wcscmp(target, ValidTargetPath) == 0)
            return TRUE;
        else
            return FALSE;
    else
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return TRUE;
        else
            return FALSE;
}

INT CRamDisk::ImDiskCliFormatDisk(LPCWSTR DevicePath, WCHAR DriveLetter, LPCWSTR FormatOptions)
{
    static const WCHAR format_mutex[] = L"ImDiskFormat";

    static const WCHAR format_cmd_prefix[] = L"C:\\Windows\\System32\\format.com ";

    WCHAR temporary_mount_point[] = { 255, L':', 0 };

#pragma warning(suppress: 6305)
    LPWSTR format_cmd = (LPWSTR)
        ImDiskCliAssertNotNull(_alloca(sizeof(format_cmd_prefix) +
        sizeof(temporary_mount_point) + (wcslen(FormatOptions) << 1)));

    STARTUPINFO startup_info = { sizeof(startup_info) };
    PROCESS_INFORMATION process_info;

    BOOL temp_drive_defined = FALSE;

    int iReturnCode;

    HANDLE hMutex = CreateMutex(NULL, FALSE, format_mutex);
    if (hMutex == NULL)
    {
        PrintLastError(L"Error creating mutex object:");
        return IMDISK_CLI_ERROR_FORMAT;
    }

    switch (WaitForSingleObject(hMutex, INFINITE))
    {
    case WAIT_OBJECT_0:
    case WAIT_ABANDONED:
        break;

    default:
        PrintLastError(L"Error, mutex object failed:");
        CloseHandle(hMutex);
        return IMDISK_CLI_ERROR_FORMAT;
    }

    if (DriveLetter != 0)
    {
        temporary_mount_point[0] = DriveLetter;
    }
    else
    {
        temporary_mount_point[0] = ImDiskFindFreeDriveLetter();

        temp_drive_defined = TRUE;
    }

    if (temporary_mount_point[0] == 0)
    {
        fprintf
            (stderr,
                "Format failed. No free drive letters available.\r\n");

        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return IMDISK_CLI_ERROR_FORMAT;
    }

    if (!ImDiskCliValidateDriveLetterTarget(temporary_mount_point,
        DevicePath))
    {
        if (!DefineDosDevice(DDD_RAW_TARGET_PATH,
            temporary_mount_point,
            DevicePath))
        {
            PrintLastError(L"Error defining drive letter:");
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return IMDISK_CLI_ERROR_FORMAT;
        }

        if (!ImDiskCliValidateDriveLetterTarget(temporary_mount_point,
            DevicePath))
        {
            if (!DefineDosDevice(DDD_REMOVE_DEFINITION |
                DDD_EXACT_MATCH_ON_REMOVE |
                DDD_RAW_TARGET_PATH,
                temporary_mount_point,
                DevicePath))
                PrintLastError(L"Error undefining temporary drive letter:");

            ReleaseMutex(hMutex);
            CloseHandle(hMutex);

            return IMDISK_CLI_ERROR_FORMAT;
        }
    }

    printf("Formatting disk %ws...\n", temporary_mount_point);

    wcscpy(format_cmd, format_cmd_prefix);
    wcscat(format_cmd, temporary_mount_point);
    wcscat(format_cmd, L" ");
    wcscat(format_cmd, FormatOptions);

    if (CreateProcess(NULL, format_cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL,
        &startup_info, &process_info))
    {
        CloseHandle(process_info.hThread);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(process_info.hProcess);
        iReturnCode = IMDISK_CLI_SUCCESS;
    }
    else
    {
        PrintLastError(L"Cannot format drive:");
        iReturnCode = IMDISK_CLI_ERROR_FORMAT;
    }

    if (temp_drive_defined)
    {
        if (!DefineDosDevice(DDD_REMOVE_DEFINITION |
            DDD_EXACT_MATCH_ON_REMOVE |
            DDD_RAW_TARGET_PATH,
            temporary_mount_point,
            DevicePath))
            PrintLastError(L"Error undefining temporary drive letter:");
    }

    if (!ReleaseMutex(hMutex))
        PrintLastError(L"Error releasing mutex:");

    if (!CloseHandle(hMutex))
        PrintLastError(L"Error releasing mutex:");

    return iReturnCode;
}

INT CRamDisk::ImDiskCliCreateDevice(LPDWORD DeviceNumber, PDISK_GEOMETRY DiskGeometry, PLARGE_INTEGER ImageOffset,
                                    DWORD Flags, LPCWSTR FileName, BOOL NativePath, LPWSTR MountPoint,
                                    BOOL NumericPrint, LPWSTR FormatOptions, BOOL SaveSettings)
{
    PIMDISK_CREATE_DATA create_data;
    HANDLE driver;
    UNICODE_STRING file_name;
    DWORD dw;
    WCHAR device_path[MAX_PATH];

    RtlInitUnicodeString(&file_name, IMDISK_CTL_DEVICE_NAME);

    for (;;)
    {
        driver = ImDiskOpenDeviceByName(&file_name,
                                        GENERIC_READ | GENERIC_WRITE);

        if (driver != INVALID_HANDLE_VALUE)
            break;

        if (GetLastError() != ERROR_FILE_NOT_FOUND)
        {
            PrintLastError(L"Error controlling the ImDisk Virtual Disk Driver:");
            return IMDISK_CLI_ERROR_DRIVER_INACCESSIBLE;
        }

        if (!ImDiskStartService((LPWSTR)IMDISK_DRIVER_NAME))
            switch (GetLastError())
            {
            case ERROR_SERVICE_DOES_NOT_EXIST:
                fputs("The ImDisk Virtual Disk Driver is not installed. "
                      "Please re-install ImDisk.\r\n", stderr);
                return IMDISK_CLI_ERROR_DRIVER_NOT_INSTALLED;

            case ERROR_PATH_NOT_FOUND:
            case ERROR_FILE_NOT_FOUND:
                fputs("Cannot load imdisk.sys. "
                      "Please re-install ImDisk.\r\n", stderr);
                return IMDISK_CLI_ERROR_DRIVER_NOT_INSTALLED;

            case ERROR_SERVICE_DISABLED:
                fputs("The ImDisk Virtual Disk Driver is disabled.\r\n", stderr);
                return IMDISK_CLI_ERROR_DRIVER_NOT_INSTALLED;

            default:
                PrintLastError(L"Error loading ImDisk Virtual Disk Driver:");
                return IMDISK_CLI_ERROR_DRIVER_NOT_INSTALLED;
            }

        Sleep(0);
        puts("The ImDisk Virtual Disk Driver was loaded into the kernel.");
    }

    if (!ImDiskCliCheckDriverVersion(driver))
    {
        CloseHandle(driver);
        return IMDISK_CLI_ERROR_DRIVER_WRONG_VERSION;
    }

    // Physical memory allocation requires the AWEAlloc driver.
    if (((IMDISK_TYPE(Flags) == IMDISK_TYPE_FILE) |
         (IMDISK_TYPE(Flags) == 0)) &
            (IMDISK_FILE_TYPE(Flags) == IMDISK_FILE_TYPE_AWEALLOC))
    {
        HANDLE awealloc;
        UNICODE_STRING file_name;

        RtlInitUnicodeString(&file_name, AWEALLOC_DEVICE_NAME);

        for (;;)
        {
            awealloc = ImDiskOpenDeviceByName(&file_name,
                                              GENERIC_READ | GENERIC_WRITE);

            if (awealloc != INVALID_HANDLE_VALUE)
            {
                NtClose(awealloc);
                break;
            }

            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                break;

            if (ImDiskStartService((LPWSTR)AWEALLOC_DRIVER_NAME))
            {
                puts("AWEAlloc driver was loaded into the kernel.");
                continue;
            }

            switch (GetLastError())
            {
            case ERROR_SERVICE_DOES_NOT_EXIST:
                fputs("The AWEAlloc driver is not installed.\r\n"
                      "Please re-install ImDisk.\r\n", stderr);
                break;

            case ERROR_PATH_NOT_FOUND:
            case ERROR_FILE_NOT_FOUND:
                fputs("Cannot load AWEAlloc driver.\r\n"
                      "Please re-install ImDisk.\r\n", stderr);
                break;

            case ERROR_SERVICE_DISABLED:
                fputs("The AWEAlloc driver is disabled.\r\n", stderr);
                break;

            default:
                PrintLastError(L"Error loading AWEAlloc driver:");
            }

            CloseHandle(driver);
            return IMDISK_CLI_ERROR_SERVICE_INACCESSIBLE;
        }
    }
    // Proxy reconnection types requires the user mode service.
    else if ((IMDISK_TYPE(Flags) == IMDISK_TYPE_PROXY) &
             ((IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_TCP) |
              (IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_COMM)))
    {
        if (!WaitNamedPipe(IMDPROXY_SVC_PIPE_DOSDEV_NAME, 0))
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                if (ImDiskStartService((LPWSTR)IMDPROXY_SVC))
                {
                    while (!WaitNamedPipe(IMDPROXY_SVC_PIPE_DOSDEV_NAME, 0))
                        if (GetLastError() == ERROR_FILE_NOT_FOUND)
                            Sleep(200);
                        else
                            break;

                    puts
                            ("The ImDisk Virtual Disk Driver Helper Service was started.");
                }
                else
                {
                    switch (GetLastError())
                    {
                    case ERROR_SERVICE_DOES_NOT_EXIST:
                        fputs("The ImDisk Virtual Disk Driver Helper Service is not "
                              "installed.\r\n"
                              "Please re-install ImDisk.\r\n", stderr);
                        break;

                    case ERROR_PATH_NOT_FOUND:
                    case ERROR_FILE_NOT_FOUND:
                        fputs("Cannot start ImDisk Virtual Disk Driver Helper "
                              "Service.\r\n"
                              "Please re-install ImDisk.\r\n", stderr);
                        break;

                    case ERROR_SERVICE_DISABLED:
                        fputs("The ImDisk Virtual Disk Driver Helper Service is "
                              "disabled.\r\n", stderr);
                        break;

                    default:
                        PrintLastError
                                (L"Error starting ImDisk Virtual Disk Driver Helper "
                                 L"Service:");
                    }

                    CloseHandle(driver);
                    return IMDISK_CLI_ERROR_SERVICE_INACCESSIBLE;
                }
    }

    if (FileName == NULL)
        RtlInitUnicodeString(&file_name, NULL);
    else if (NativePath)
    {
        if (!RtlCreateUnicodeString(&file_name, FileName))
        {
            CloseHandle(driver);
            fputs("Memory allocation error.\r\n", stderr);
            return IMDISK_CLI_ERROR_FATAL;
        }
    }
    else if ((IMDISK_TYPE(Flags) == IMDISK_TYPE_PROXY) &
             (IMDISK_PROXY_TYPE(Flags) == IMDISK_PROXY_TYPE_SHM))
    {
        LPWSTR namespace_prefix;
        LPWSTR prefixed_name;
        HANDLE h = CreateFile(L"\\\\?\\Global", 0, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if ((h == INVALID_HANDLE_VALUE) &
                (GetLastError() == ERROR_FILE_NOT_FOUND))
            namespace_prefix = (LPWSTR)L"\\BaseNamedObjects\\";
        else
            namespace_prefix = (LPWSTR)L"\\BaseNamedObjects\\Global\\";

        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);

        prefixed_name = (LPWSTR) //-V635
                _alloca(((wcslen(namespace_prefix) + wcslen(FileName)) << 1) + 1);

        if (prefixed_name == NULL)
        {
            CloseHandle(driver);
            fputs("Memory allocation error.\r\n", stderr);
            return IMDISK_CLI_ERROR_FATAL;
        }

        wcscpy(prefixed_name, namespace_prefix);
        wcscat(prefixed_name, FileName);

        if (!RtlCreateUnicodeString(&file_name, prefixed_name))
        {
            CloseHandle(driver);
            fputs("Memory allocation error.\r\n", stderr);
            return IMDISK_CLI_ERROR_FATAL;
        }
    }
    else
    {
        if (!RtlDosPathNameToNtPathName_U(FileName, &file_name, NULL, NULL))
        {
            CloseHandle(driver);
            fputs("Memory allocation error.\r\n", stderr);
            return IMDISK_CLI_ERROR_FATAL;
        }
    }

    create_data = static_cast<PIMDISK_CREATE_DATA>(alloca(sizeof(IMDISK_CREATE_DATA) + file_name.Length));
    if (create_data == NULL)
    {
        perror("Memory allocation error");
        CloseHandle(driver);
        RtlFreeUnicodeString(&file_name);
        return IMDISK_CLI_ERROR_FATAL;
    }

    ZeroMemory(create_data, sizeof(IMDISK_CREATE_DATA) + file_name.Length);

    puts("Creating device...");

    // Check if mount point is a drive letter or junction point
    if (MountPoint != NULL)
        if ((wcslen(MountPoint) == 2) ? MountPoint[1] == ':' :
                (wcslen(MountPoint) == 3) ? wcscmp(MountPoint + 1, L":\\") == 0 :
                FALSE)
            create_data->DriveLetter = MountPoint[0];

    create_data->DeviceNumber = *DeviceNumber;
    create_data->DiskGeometry = *DiskGeometry;
    create_data->ImageOffset = *ImageOffset;
    create_data->Flags = Flags;
    create_data->FileNameLength = file_name.Length;

    if (file_name.Length != 0)
    {
        memcpy(&create_data->FileName, file_name.Buffer, file_name.Length);
        RtlFreeUnicodeString(&file_name);
    }

    if (!DeviceIoControl(driver,
                         IOCTL_IMDISK_CREATE_DEVICE,
                         create_data,
                         sizeof(IMDISK_CREATE_DATA) +
                         create_data->FileNameLength,
                         create_data,
                         sizeof(IMDISK_CREATE_DATA) +
                         create_data->FileNameLength,
                         &dw,
                         NULL))
    {
        PrintLastError(L"Error creating virtual disk:");
        CloseHandle(driver);
        return IMDISK_CLI_ERROR_CREATE_DEVICE;
    }

    CloseHandle(driver);

    *DeviceNumber = create_data->DeviceNumber;

    // Build device path, e.g. \Device\ImDisk2
    _snwprintf(device_path, sizeof(device_path) / sizeof(*device_path) - 1,
               IMDISK_DEVICE_BASE_NAME L"%u", create_data->DeviceNumber);
    device_path[sizeof(device_path) / sizeof(*device_path) - 1] = 0;

    if (MountPoint != NULL)
    {
        if (create_data->DriveLetter == 0)
        {
            if (!ImDiskCreateMountPoint(MountPoint, device_path))
            {
                switch (GetLastError())
                {
                case ERROR_INVALID_REPARSE_DATA:
                    ImDiskOemPrintF(stderr,
                                    "Invalid mount point path: '%1!ws!'\n",
                                    MountPoint);
                    break;

                case ERROR_INVALID_PARAMETER:
                    fputs("This version of Windows only supports drive letters "
                          "as mount points.\r\n"
                          "Windows 2000 or higher is required to support "
                          "subdirectory mount points.\r\n",
                          stderr);
                    break;

                case ERROR_INVALID_FUNCTION:
                case ERROR_NOT_A_REPARSE_POINT:
                    fputs("Mount points are only supported on NTFS volumes.\r\n",
                          stderr);
                    break;

                case ERROR_DIRECTORY:
                case ERROR_DIR_NOT_EMPTY:
                    fputs("Mount points can only be created on empty "
                          "directories.\r\n", stderr);
                    break;

                default:
                    PrintLastError(L"Error creating mount point:");
                }

                fputs
                        ("Warning: The device is created without a mount point.\r\n",
                         stderr);

                MountPoint[0] = 0;
            }
        }
#ifndef _WIN64
        else if (!IMDISK_GTE_WINXP())
            if (!DefineDosDevice(DDD_RAW_TARGET_PATH, MountPoint, device_path))
                PrintLastError(L"Error creating mount point:");
#endif

    }

    if (NumericPrint)
        printf("%u\n", *DeviceNumber);
    else
        ImDiskOemPrintF(stdout,
                        "Created device %1!u!: %2!ws! -> %3!ws!",
                        *DeviceNumber,
                        MountPoint == NULL ? L"No mountpoint" : MountPoint,
                        FileName == NULL ? L"Image in memory" : FileName);

    if (SaveSettings)
    {
        puts("Saving registry settings...");
        if (!ImDiskSaveRegistrySettings(create_data))
            PrintLastError(L"Registry edit failed");
    }

    if (FormatOptions != NULL)
        return ImDiskCliFormatDisk(device_path,
                                   create_data->DriveLetter,
                                   FormatOptions);

    return IMDISK_CLI_SUCCESS;
}

INT CRamDisk::ImDiskCliRemoveDevice(DWORD DeviceNumber, LPCWSTR MountPoint, BOOL ForceDismount, BOOL EmergencyRemove, BOOL RemoveSettings)
{
    WCHAR drive_letter_mount_point[] = L" :";
    DWORD dw;

    if (EmergencyRemove)
    {
        puts("Emergency removal...");

        if (!ImDiskForceRemoveDevice(NULL, DeviceNumber))
        {
            PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
            return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
        }
    }
    else
    {
        PIMDISK_CREATE_DATA create_data = (PIMDISK_CREATE_DATA)
                _alloca(sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2));
        HANDLE device;

        if (create_data == NULL)
        {
            perror("Memory allocation error");
            return IMDISK_CLI_ERROR_FATAL;
        }

        if (MountPoint == NULL)
        {
            device = ImDiskOpenDeviceByNumber(DeviceNumber,
                                              GENERIC_READ | GENERIC_WRITE);

            if (device == INVALID_HANDLE_VALUE)
                device = ImDiskOpenDeviceByNumber(DeviceNumber,
                                                  GENERIC_READ);

            if (device == INVALID_HANDLE_VALUE)
                device = ImDiskOpenDeviceByNumber(DeviceNumber,
                                                  FILE_READ_ATTRIBUTES);
        }
        else if ((wcslen(MountPoint) == 2) ? MountPoint[1] == ':' :
                 (wcslen(MountPoint) == 3) ? wcscmp(MountPoint + 1, L":\\") == 0
                 : FALSE)
        {
            WCHAR drive_letter_path[] = L"\\\\.\\ :";
            drive_letter_path[4] = MountPoint[0];

            // Notify processes that this device is about to be removed.
            if ((MountPoint[0] >= L'A') & (MountPoint[0] <= L'Z'))
            {
                puts("Notifying applications...");

                ImDiskNotifyRemovePending(NULL, MountPoint[0]);
            }

            DbgOemPrintF((stdout, "Opening %1!ws!...\n", MountPoint));

            device = CreateFile(drive_letter_path,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING,
                                NULL);

            if (device == INVALID_HANDLE_VALUE)
                device = CreateFile(drive_letter_path,
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING,
                                    NULL);

            if (device == INVALID_HANDLE_VALUE)
                device = CreateFile(drive_letter_path,
                                    FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING,
                                    NULL);
        }
        else
        {
            device = ImDiskOpenDeviceByMountPoint(MountPoint,
                                                  GENERIC_READ | GENERIC_WRITE);

            if (device == INVALID_HANDLE_VALUE)
                device = ImDiskOpenDeviceByMountPoint(MountPoint,
                                                      GENERIC_READ);

            if (device == INVALID_HANDLE_VALUE)
                device = ImDiskOpenDeviceByMountPoint(MountPoint,
                                                      FILE_READ_ATTRIBUTES);

            if (device == INVALID_HANDLE_VALUE)
                switch (GetLastError())
                {
                case ERROR_INVALID_PARAMETER:
                    fputs("This version of Windows only supports drive letters as "
                          "mount points.\r\n"
                          "Windows 2000 or higher is required to support "
                          "subdirectory mount points.\r\n",
                          stderr);
                    return IMDISK_CLI_ERROR_BAD_MOUNT_POINT;

                case ERROR_INVALID_FUNCTION:
                    fputs("Mount points are only supported on NTFS volumes.\r\n",
                          stderr);
                    return IMDISK_CLI_ERROR_BAD_MOUNT_POINT;

                case ERROR_NOT_A_REPARSE_POINT:
                case ERROR_DIRECTORY:
                case ERROR_DIR_NOT_EMPTY:
                    ImDiskOemPrintF(stderr, "Not a mount point: '%1!ws!'\n",
                                    MountPoint);
                    return IMDISK_CLI_ERROR_BAD_MOUNT_POINT;

                default:
                    PrintLastError(MountPoint);
                    return IMDISK_CLI_ERROR_BAD_MOUNT_POINT;
                }
        }

        if (device == INVALID_HANDLE_VALUE)
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                fputs("No such device.\r\n", stderr);
                return IMDISK_CLI_ERROR_DEVICE_NOT_FOUND;
            }
            else
            {
                PrintLastError(L"Error opening device:");
                return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
            }

        if (!ImDiskCliCheckDriverVersion(device))
        {
            CloseHandle(device);
            return IMDISK_CLI_ERROR_DRIVER_WRONG_VERSION;
        }

        if (!DeviceIoControl(device,
                             IOCTL_IMDISK_QUERY_DEVICE,
                             NULL,
                             0,
                             create_data,
                             sizeof(IMDISK_CREATE_DATA) + (MAX_PATH << 2),
                             &dw, NULL))
        {
            PrintLastError(MountPoint);
            ImDiskOemPrintF(stderr,
                            "%1!ws!: Is that drive really an ImDisk drive?",
                            MountPoint);
            return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
        }

        if (dw < sizeof(IMDISK_CREATE_DATA) - sizeof(*create_data->FileName))
        {
            ImDiskOemPrintF(stderr,
                            "%1!ws!: Is that drive really an ImDisk drive?",
                            MountPoint);
            return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
        }

        if ((MountPoint == NULL) & (create_data->DriveLetter != 0))
        {
            drive_letter_mount_point[0] = create_data->DriveLetter;
            MountPoint = drive_letter_mount_point;
        }

        if (RemoveSettings)
        {
            printf("Removing registry settings for device %u...\n",
                   create_data->DeviceNumber);
            if (!ImDiskRemoveRegistrySettings(create_data->DeviceNumber))
                PrintLastError(L"Registry edit failed");
        }

        puts("Flushing file buffers...");

        FlushFileBuffers(device);

        puts("Locking volume...");

        if (!DeviceIoControl(device,
                             FSCTL_LOCK_VOLUME,
                             NULL,
                             0,
                             NULL,
                             0,
                             &dw,
                             NULL))
            if (ForceDismount)
            {
                puts("Failed, forcing dismount...");

                DeviceIoControl(device,
                                FSCTL_DISMOUNT_VOLUME,
                                NULL,
                                0,
                                NULL,
                                0,
                                &dw,
                                NULL);

                DeviceIoControl(device,
                                FSCTL_LOCK_VOLUME,
                                NULL,
                                0,
                                NULL,
                                0,
                                &dw,
                                NULL);
            }
            else
            {
                PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
                return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
            }
        else
        {
            puts("Dismounting filesystem...");

            if (!DeviceIoControl(device,
                                 FSCTL_DISMOUNT_VOLUME,
                                 NULL,
                                 0,
                                 NULL,
                                 0,
                                 &dw,
                                 NULL))
            {
                PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
                return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
            }
        }

        puts("Removing device...");

        if (!DeviceIoControl(device,
                             IOCTL_STORAGE_EJECT_MEDIA,
                             NULL,
                             0,
                             NULL,
                             0,
                             &dw,
                             NULL))
            if (ForceDismount ? !ImDiskForceRemoveDevice(device, 0) : FALSE)
            {
                PrintLastError(MountPoint == NULL ? L"Error" : MountPoint);
                return IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE;
            }

        DeviceIoControl(device,
                        FSCTL_UNLOCK_VOLUME,
                        NULL,
                        0,
                        NULL,
                        0,
                        &dw,
                        NULL);

        CloseHandle(device);
    }

    if (MountPoint != NULL)
    {
        puts("Removing mountpoint...");

        if (!ImDiskRemoveMountPoint(MountPoint))
        {
            switch (GetLastError())
            {
            case ERROR_INVALID_PARAMETER:
                fputs("This version of Windows only supports drive letters as "
                      "mount points.\r\n"
                      "Windows 2000 or higher is required to support "
                      "subdirectory mount points.\r\n",
                      stderr);
                break;

            case ERROR_INVALID_FUNCTION:
                fputs("Mount points are only supported on empty directories "
                      "on NTFS volumes.\r\n",
                      stderr);
                break;

            case ERROR_NOT_A_REPARSE_POINT:
            case ERROR_DIRECTORY:
            case ERROR_DIR_NOT_EMPTY:
                ImDiskOemPrintF(stderr,
                                "Not a mount point: '%1!ws!'\n", MountPoint);
                break;

            default:
                PrintLastError(MountPoint);
            }
        }
    }

    puts("Done.");

    return 0;
}
