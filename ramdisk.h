#ifndef CRAMDISK_H
#define CRAMDISK_H

#include <QObject>
#include <QChar>
#include <QDebug>
#include <QProcess>

#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dbt.h>

#include <stdio.h>
#include <stdlib.h>

// ImDisk includes
#include <ntumapi.h>
#include <imdisk.h>
#include <imdproxy.h>

enum
{
    IMDISK_CLI_SUCCESS = 0,
    IMDISK_CLI_ERROR_DEVICE_NOT_FOUND = 1,
    IMDISK_CLI_ERROR_DEVICE_INACCESSIBLE = 2,
    IMDISK_CLI_ERROR_CREATE_DEVICE = 3,
    IMDISK_CLI_ERROR_DRIVER_NOT_INSTALLED = 4,
    IMDISK_CLI_ERROR_DRIVER_WRONG_VERSION = 5,
    IMDISK_CLI_ERROR_DRIVER_INACCESSIBLE = 6,
    IMDISK_CLI_ERROR_SERVICE_INACCESSIBLE = 7,
    IMDISK_CLI_ERROR_FORMAT = 8,
    IMDISK_CLI_ERROR_BAD_MOUNT_POINT = 9,
    IMDISK_CLI_ERROR_BAD_SYNTAX = 10,
    IMDISK_CLI_ERROR_NOT_ENOUGH_MEMORY = 11,
    IMDISK_CLI_ERROR_PARTITION_NOT_FOUND = 12,
    IMDISK_CLI_ERROR_WRONG_SYNTAX = 13,
    IMDISK_CLI_ERROR_FATAL = -1
};

#define DbgOemPrintF(x)

// Wrapper for ImDisk
class CRamDisk : public QObject
{
    Q_OBJECT
protected:
    explicit CRamDisk(QObject *parent = 0);
    ~CRamDisk();

public slots:
    void unmount();

public:
    void mount();
    bool wasMounted();
    static CRamDisk* getInstance();
    static void destroyInstance();
    void init();

private:
    static const QString driveLetter;
    static const quint64 driveSize;
    static const QString driveFileSystem;
    bool _wasMounted;
    static CRamDisk *_instance;


// ============================================
// WinAPI, C-style code, (Hungarian Notation)
// ============================================
private:
    DWORD _deviceNumber;
    DISK_GEOMETRY _diskGeometry;
    LARGE_INTEGER _imageOffset;

private:
    INT ImDiskCliRemoveDevice(DWORD DeviceNumber, LPCWSTR MountPoint, BOOL ForceDismount, BOOL EmergencyRemove, BOOL RemoveSettings);
    INT ImDiskCliCreateDevice(LPDWORD DeviceNumber, PDISK_GEOMETRY DiskGeometry, PLARGE_INTEGER ImageOffset,
                              DWORD Flags, LPCWSTR FileName, BOOL NativePath, LPWSTR MountPoint,
                              BOOL NumericPrint, LPWSTR FormatOptions, BOOL SaveSettings);

    INT ImDiskCliFormatDisk(LPCWSTR DevicePath, WCHAR DriveLetter, LPCWSTR FormatOptions);

    BOOL ImDiskOemPrintF(FILE *Stream, LPCSTR Message, ...);
    VOID PrintLastError(LPCWSTR Prefix);
    LPVOID ImDiskCliAssertNotNull(LPVOID Ptr);
    BOOL ImDiskCliCheckDriverVersion(HANDLE Device);
    BOOL ImDiskCliValidateDriveLetterTarget(LPCWSTR DriveLetter, LPCWSTR ValidTargetPath);
};

#endif // CRAMDISK_H
