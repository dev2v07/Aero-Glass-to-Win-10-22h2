#include <Uefi.h>

#include <Guid/FileInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#define AEROBOOT_FRAME_COUNT 105
#define AEROBOOT_FRAME_WIDTH 200
#define AEROBOOT_FRAME_HEIGHT 200
#define AEROBOOT_FRAME_DELAY_US 66666
#define AEROBOOT_MAX_FILE_SIZE (8U * 1024U * 1024U)
#define AEROBOOT_MAX_BITMAP_DIMENSION 4096U
#define AEROBOOT_WATCHDOG_TIMEOUT_SECONDS 20U

typedef struct {
	UINT8* Data;
	UINTN Size;
	UINT32 Width;
	UINT32 Height;
	UINT32 RowStride;
	BOOLEAN TopDown;
} AEROBOOT_BITMAP;

STATIC EFI_STATUS OpenAeroBootRoot(
	IN EFI_HANDLE ImageHandle,
	OUT EFI_FILE_PROTOCOL** Root
)
{
	EFI_STATUS Status;
	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = NULL;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem = NULL;

	Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
	if (EFI_ERROR(Status)) return Status;
	Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
	if (EFI_ERROR(Status)) return Status;
	return FileSystem->OpenVolume(FileSystem, Root);
}

STATIC EFI_STATUS ReadFile(
	IN EFI_FILE_PROTOCOL* Root,
	IN CHAR16* Path,
	OUT VOID** Buffer,
	OUT UINTN* BufferSize
)
{
	EFI_STATUS Status;
	EFI_FILE_PROTOCOL* File = NULL;
	EFI_FILE_INFO* Info = NULL;
	UINTN InfoSize = 0;
	UINTN Size;

	*Buffer = NULL;
	*BufferSize = 0;
	Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(Status)) return Status;
	Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
	if (Status != EFI_BUFFER_TOO_SMALL) goto Done;
	Info = AllocatePool(InfoSize);
	if (Info == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
	Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
	if (EFI_ERROR(Status)) goto Done;
	if (Info->FileSize == 0 || Info->FileSize > AEROBOOT_MAX_FILE_SIZE ||
		Info->FileSize > (UINT64)MAX_UINTN) {
		Status = EFI_BAD_BUFFER_SIZE;
		goto Done;
	}
	Size = (UINTN)Info->FileSize;
	*Buffer = AllocatePool(Size);
	if (*Buffer == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
	*BufferSize = Size;
	Status = File->Read(File, BufferSize, *Buffer);
	if (EFI_ERROR(Status) || *BufferSize != Size) {
		FreePool(*Buffer);
		*Buffer = NULL;
		*BufferSize = 0;
	}

Done:
	if (Info != NULL) FreePool(Info);
	if (File != NULL) File->Close(File);
	return Status;
}

STATIC EFI_STATUS DecodeBmp24(
	IN VOID* Data,
	IN UINTN DataSize,
	OUT AEROBOOT_BITMAP* Bitmap
)
{
	UINT8* Bytes = (UINT8*)Data;
	UINT32 PixelOffset;
	INT32 Width;
	INT32 Height;
	UINT16 BitsPerPixel;
	UINT16 Planes;
	UINT32 HeaderSize;
	UINT32 Compression;
	UINT32 RowStride;
	UINT64 AbsoluteHeight;
	UINT64 RequiredSize;

	SetMem(Bitmap, sizeof(*Bitmap), 0);
	if (DataSize < 54 || Bytes[0] != 'B' || Bytes[1] != 'M') return EFI_UNSUPPORTED;
	CopyMem(&PixelOffset, Bytes + 10, sizeof(PixelOffset));
	CopyMem(&HeaderSize, Bytes + 14, sizeof(HeaderSize));
	CopyMem(&Width, Bytes + 18, sizeof(Width));
	CopyMem(&Height, Bytes + 22, sizeof(Height));
	CopyMem(&Planes, Bytes + 26, sizeof(Planes));
	CopyMem(&BitsPerPixel, Bytes + 28, sizeof(BitsPerPixel));
	CopyMem(&Compression, Bytes + 30, sizeof(Compression));
	if (HeaderSize < 40 || Planes != 1 || Width <= 0 || Height == 0 ||
		BitsPerPixel != 24 || Compression != 0) return EFI_UNSUPPORTED;
	AbsoluteHeight = Height < 0 ? (UINT64)(-(INT64)Height) : (UINT64)Height;
	if ((UINT32)Width > AEROBOOT_MAX_BITMAP_DIMENSION ||
		AbsoluteHeight > AEROBOOT_MAX_BITMAP_DIMENSION ||
		(UINT32)Width != AEROBOOT_FRAME_WIDTH ||
		AbsoluteHeight != AEROBOOT_FRAME_HEIGHT) return EFI_UNSUPPORTED;
	if (HeaderSize > DataSize - 14 || PixelOffset < 14 + HeaderSize || PixelOffset > DataSize) {
		return EFI_COMPROMISED_DATA;
	}
	RequiredSize = ((UINT64)(UINT32)Width * 3U + 3U) & ~3ULL;
	RequiredSize *= AbsoluteHeight;
	if (RequiredSize > (UINT64)MAX_UINT32 || (UINT64)PixelOffset + RequiredSize > DataSize) {
		return EFI_COMPROMISED_DATA;
	}
	RowStride = (UINT32)RequiredSize / (UINT32)AbsoluteHeight;
	Bitmap->Data = Bytes + PixelOffset;
	Bitmap->Size = DataSize - PixelOffset;
	Bitmap->Width = (UINT32)Width;
	Bitmap->Height = (UINT32)AbsoluteHeight;
	Bitmap->RowStride = RowStride;
	Bitmap->TopDown = Height < 0;
	return EFI_SUCCESS;
}

STATIC EFI_STATUS DrawBitmap(
	IN EFI_GRAPHICS_OUTPUT_PROTOCOL* Graphics,
	IN AEROBOOT_BITMAP* Bitmap
)
{
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Mode = Graphics->Mode->Info;
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Pixels;
	UINT32 X;
	UINT32 Y;
	UINT32 SourceY;
	UINT32 SourceX;
	UINT32 DrawWidth;
	UINT32 DrawHeight;
	UINT64 PixelCount64;
	UINT64 AllocationSize;
	EFI_STATUS Status;

	PixelCount64 = (UINT64)Bitmap->Width * Bitmap->Height;
	AllocationSize = PixelCount64 * sizeof(*Pixels);
	if (PixelCount64 == 0 || AllocationSize > (UINT64)MAX_UINTN) return EFI_BAD_BUFFER_SIZE;
	Pixels = AllocateZeroPool((UINTN)AllocationSize);
	if (Pixels == NULL) return EFI_OUT_OF_RESOURCES;
	X = Mode->HorizontalResolution > Bitmap->Width ? (Mode->HorizontalResolution - Bitmap->Width) / 2 : 0;
	Y = Mode->VerticalResolution > Bitmap->Height ? (Mode->VerticalResolution - Bitmap->Height) / 2 : 0;
	DrawWidth = Bitmap->Width < Mode->HorizontalResolution ? Bitmap->Width : Mode->HorizontalResolution;
	DrawHeight = Bitmap->Height < Mode->VerticalResolution ? Bitmap->Height : Mode->VerticalResolution;
	for (SourceY = 0; SourceY < DrawHeight; ++SourceY)
	{
		CONST UINT8* Row = Bitmap->Data + (Bitmap->TopDown ? SourceY : Bitmap->Height - SourceY - 1) * Bitmap->RowStride;
		for (SourceX = 0; SourceX < DrawWidth; ++SourceX)
		{
			EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Pixel = &Pixels[SourceY * Bitmap->Width + SourceX];
			Pixel->Blue = Row[SourceX * 3 + 0];
			Pixel->Green = Row[SourceX * 3 + 1];
			Pixel->Red = Row[SourceX * 3 + 2];
			Pixel->Reserved = 0;
		}
	}
	Status = Graphics->Blt(Graphics, Pixels, EfiBltBufferToVideo, 0, 0, X, Y,
		DrawWidth, DrawHeight, Bitmap->Width * sizeof(*Pixels));
	FreePool(Pixels);
	return Status;
}

STATIC EFI_STATUS ChainloadWindows(
	IN EFI_HANDLE ImageHandle,
	IN CHAR16* Path
)
{
	EFI_STATUS Status;
	EFI_HANDLE Child = NULL;
	EFI_DEVICE_PATH_PROTOCOL* DevicePath;
	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = NULL;

	Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
	if (EFI_ERROR(Status)) return Status;
	Print(L"AeroBoot: chainload %s\r\n", Path);
	DevicePath = FileDevicePath(LoadedImage->DeviceHandle, Path);
	if (DevicePath == NULL) return EFI_OUT_OF_RESOURCES;
	Status = gBS->LoadImage(FALSE, ImageHandle, DevicePath, NULL, 0, &Child);
	FreePool(DevicePath);
	if (EFI_ERROR(Status)) {
		Print(L"AeroBoot: LoadImage failed for %s: %r\r\n", Path, Status);
		return Status;
	}
	Status = gBS->StartImage(Child, NULL, NULL);
	Print(L"AeroBoot: StartImage returned: %r\r\n", Status);
	if (EFI_ERROR(Status)) gBS->UnloadImage(Child);
	return Status;
}

STATIC EFI_STATUS ChainloadWithFallback(IN EFI_HANDLE ImageHandle)
{
	EFI_STATUS Status;
	Status = ChainloadWindows(ImageHandle, L"\\EFI\\AeroBoot\\bootmgfw-original.efi");
	if (!EFI_ERROR(Status)) return Status;
	Print(L"AeroBoot: original loader unavailable, trying Microsoft loader\r\n");
	return ChainloadWindows(ImageHandle, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
}

EFI_STATUS EFIAPI UefiMain(
	IN EFI_HANDLE ImageHandle,
	IN EFI_SYSTEM_TABLE* SystemTable
)
{
	EFI_STATUS Status;
	EFI_FILE_PROTOCOL* Root = NULL;
	EFI_GRAPHICS_OUTPUT_PROTOCOL* Graphics = NULL;
	AEROBOOT_BITMAP Bitmap;
	VOID* Buffer = NULL;
	UINTN BufferSize = 0;
	CHAR16 Path[96];
	UINTN Frame;

	Print(L"AeroBoot: start\r\n");
	Status = gBS->SetWatchdogTimer(AEROBOOT_WATCHDOG_TIMEOUT_SECONDS, 0, 0, NULL);
	if (EFI_ERROR(Status)) Print(L"AeroBoot: watchdog setup failed: %r\r\n", Status);

	Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&Graphics);
	if (EFI_ERROR(Status)) {
		Print(L"AeroBoot: GOP unavailable: %r\r\n", Status);
		goto Chainload;
	}
	Print(L"AeroBoot: GOP %ux%u\r\n", Graphics->Mode->Info->HorizontalResolution,
		Graphics->Mode->Info->VerticalResolution);
	Status = OpenAeroBootRoot(ImageHandle, &Root);
	if (EFI_ERROR(Status)) {
		Print(L"AeroBoot: filesystem unavailable: %r\r\n", Status);
		goto Chainload;
	}
	Print(L"AeroBoot: playing %u frames\r\n", AEROBOOT_FRAME_COUNT);

	for (Frame = 0; Frame < AEROBOOT_FRAME_COUNT; ++Frame)
	{
		UnicodeSPrint(Path, sizeof(Path), L"\\EFI\\AeroBoot\\animation\\activity%03u.bmp", Frame);
		Status = ReadFile(Root, Path, &Buffer, &BufferSize);
		if (EFI_ERROR(Status)) break;
		Status = DecodeBmp24(Buffer, BufferSize, &Bitmap);
		if (!EFI_ERROR(Status)) Status = DrawBitmap(Graphics, &Bitmap);
		FreePool(Buffer);
		Buffer = NULL;
		BufferSize = 0;
		if (EFI_ERROR(Status)) {
			Print(L"AeroBoot: frame %u failed: %r\r\n", Frame, Status);
			break;
		}
		gBS->Stall(AEROBOOT_FRAME_DELAY_US);
	}

	if (EFI_ERROR(Status))
	{
		if (Root != NULL) Root->Close(Root);
		goto Chainload;
	}
	Print(L"AeroBoot: animation complete\r\n");

Chainload:
	gBS->SetWatchdogTimer(0, 0, 0, NULL);
	Status = ChainloadWithFallback(ImageHandle);
	if (Root != NULL) Root->Close(Root);
	return Status;
}
