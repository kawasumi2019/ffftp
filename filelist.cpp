﻿/*=============================================================================
*
*								ファイル一覧
*
===============================================================================
/ Copyright (C) 1997-2007 Sota. All rights reserved.
/
/ Redistribution and use in source and binary forms, with or without 
/ modification, are permitted provided that the following conditions 
/ are met:
/
/  1. Redistributions of source code must retain the above copyright 
/     notice, this list of conditions and the following disclaimer.
/  2. Redistributions in binary form must reproduce the above copyright 
/     notice, this list of conditions and the following disclaimer in the 
/     documentation and/or other materials provided with the distribution.
/
/ THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
/ IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
/ OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
/ IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
/ INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
/ BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
/ USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
/ ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
/ (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
/ THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/============================================================================*/

#include "common.h"
#include <sys/stat.h>
#include "OleDragDrop.h"
#include "filelist.h"

#define BUF_SIZE		256
#define CF_CNT 2
#define WM_DRAGDROP		(WM_APP + 100)
#define WM_GETDATA		(WM_APP + 101)
#define WM_DRAGOVER		(WM_APP + 102)

/*===== プロトタイプ =====*/

static LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK RemoteWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT FileListCommonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static void DispFileList2View(HWND hWnd, std::vector<FILELIST>& files);
static void AddListView(HWND hWnd, int Pos, std::wstring const& Name, int Type, LONGLONG Size, FILETIME *Time, int Attr, std::wstring const& Owner, int Link, int InfoExist, int ImageId);
static int FindNameNode(int Win, std::wstring const& name);
static bool GetNodeTime(int Win, int Pos, FILETIME& ft);
static int GetImageIndex(int Win, int Pos);
static int MakeRemoteTree1(std::wstring const& Path, std::wstring const& Cur, std::vector<FILELIST>& Base, int *CancelCheckWork);
static int MakeRemoteTree2(std::wstring& Path, std::wstring const& Cur, std::vector<FILELIST>& Base, int *CancelCheckWork);
static void CopyTmpListToFileList(std::vector<FILELIST>& Base, std::vector<FILELIST> const& List);
static std::optional<std::vector<std::variant<FILELIST, std::string>>> GetListLine(int Num);
static bool MakeLocalTree(fs::path const& path, std::vector<FILELIST>& Base);
static void AddFileList(FILELIST const& Pkt, std::vector<FILELIST>& Base);
static int AskFilterStr(std::wstring const& file, int Type);

/*===== 外部参照 =====*/

extern int SepaWidth;
extern int RemoteWidth;
extern int ListHeight;
extern std::wstring FilterStr;
// 外部アプリケーションへドロップ後にローカル側のファイル一覧に作業フォルダが表示されるバグ対策
extern int SuppressRefresh;
// ローカル側自動更新
extern HANDLE ChangeNotification;
// 特定の操作を行うと異常終了するバグ修正
extern int CancelFlg;

/* 設定値 */
extern int LocalWidth;
extern int LocalTabWidth[4];
extern int RemoteTabWidth[6];
extern HFONT ListFont;
extern int ListType;
extern int FindMode;
extern int DotFile;
extern int DispDrives;
extern int MoveMode;
// ファイルアイコン表示対応
extern int DispFileIcon;
// タイムスタンプのバグ修正
extern int DispTimeSeconds;
// ファイルの属性を数字で表示
extern int DispPermissionsNumber;
extern HOSTDATA CurHost;

/*===== ローカルなワーク =====*/

static HWND hWndListLocal = NULL;
static HWND hWndListRemote = NULL;

static WNDPROC LocalProcPtr;
static WNDPROC RemoteProcPtr;

static HIMAGELIST ListImg = NULL;
// ファイルアイコン表示対応
static HIMAGELIST ListImgFileIcon = NULL;

static auto FindStr = L"*"s;		/* 検索文字列 */

static int Dragging = NO;
// 特定の操作を行うと異常終了するバグ修正
static POINT DropPoint;

static int StratusMode;			/* 0=ファイル, 1=ディレクトリ, 2=リンク */


// リモートファイルリスト (2007.9.3 yutaka)
static std::vector<FILELIST> remoteFileListBase;
static std::vector<FILELIST> remoteFileListBaseNoExpand;
static fs::path remoteFileDir;

template<class Fn>
static inline bool FindFile(fs::path const& fileName, Fn&& fn) {
	auto result = false;
	WIN32_FIND_DATAW data;
	if (auto handle = FindFirstFileW(fileName.c_str(), &data); handle != INVALID_HANDLE_VALUE) {
		result = true;
		do {
			if (DispIgnoreHide == YES && (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
				continue;
			if (std::wstring_view filename{ data.cFileName }; filename == L"."sv || filename == L".."sv)
				continue;
			result = fn(data);
		} while (result && FindNextFileW(handle, &data));
		FindClose(handle);
	} else {
		auto lastError = GetLastError();
		SetTaskMsg("FindFile failed (%s): 0x%08X, %s", fileName.u8string().c_str(), lastError, u8(GetErrorMessage(lastError)).c_str());
	}
	return result;
}

// ファイルリストウインドウを作成する
int MakeListWin()
{
	int Sts;

	/*===== ローカル側のリストビュー =====*/

	hWndListLocal = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS, 0, AskToolWinHeight() * 2, LocalWidth, ListHeight, GetMainHwnd(), 0, GetFtpInst(), nullptr);

	if(hWndListLocal != NULL)
	{
		LocalProcPtr = (WNDPROC)SetWindowLongPtrW(hWndListLocal, GWLP_WNDPROC, (LONG_PTR)LocalWndProc);

		SendMessageW(hWndListLocal, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

		if(ListFont != NULL)
			SendMessageW(hWndListLocal, WM_SETFONT, (WPARAM)ListFont, MAKELPARAM(TRUE, 0));

		ListImg = ImageList_LoadImageW(GetFtpInst(), MAKEINTRESOURCEW(dirattr_bmp), 16, 9, RGB(255,0,0), IMAGE_BITMAP, 0);
		SendMessageW(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListLocal, SW_SHOW);

		constexpr std::tuple<int, int> columns[] = {
			{ LVCFMT_LEFT, IDS_MSGJPN038 },
			{ LVCFMT_LEFT, IDS_MSGJPN039 },
			{ LVCFMT_RIGHT, IDS_MSGJPN040 },
			{ LVCFMT_LEFT, IDS_MSGJPN041 },
		};
		int i = 0;
		for (auto [fmt, resourceId] : columns) {
			auto text = GetString(resourceId);
			LVCOLUMNW column{ LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, fmt, LocalTabWidth[i], data(text), 0, i };
			SendMessageW(hWndListLocal, LVM_INSERTCOLUMNW, i++, (LPARAM)&column);
		}
	}

	/*===== ホスト側のリストビュー =====*/

	hWndListRemote = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS, LocalWidth + SepaWidth, AskToolWinHeight() * 2, RemoteWidth, ListHeight, GetMainHwnd(), 0, GetFtpInst(), nullptr);

	if(hWndListRemote != NULL)
	{
		RemoteProcPtr = (WNDPROC)SetWindowLongPtrW(hWndListRemote, GWLP_WNDPROC, (LONG_PTR)RemoteWndProc);

		SendMessageW(hWndListRemote, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

		if(ListFont != NULL)
			SendMessageW(hWndListRemote, WM_SETFONT, (WPARAM)ListFont, MAKELPARAM(TRUE, 0));

		SendMessageW(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListRemote, SW_SHOW);

		constexpr std::tuple<int, int> columns[] = {
			{ LVCFMT_LEFT, IDS_MSGJPN042 },
			{ LVCFMT_LEFT, IDS_MSGJPN043 },
			{ LVCFMT_RIGHT, IDS_MSGJPN044 },
			{ LVCFMT_LEFT, IDS_MSGJPN045 },
			{ LVCFMT_LEFT, IDS_MSGJPN046 },
			{ LVCFMT_LEFT, IDS_MSGJPN047 },
		};
		int i = 0;
		for (auto [fmt, resourceId] : columns) {
			auto text = GetString(resourceId);
			LVCOLUMNW column{ LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, fmt, RemoteTabWidth[i], data(text), 0, i };
			SendMessageW(hWndListRemote, LVM_INSERTCOLUMNW, i++, (LPARAM)&column);
		}
	}

	Sts = FFFTP_SUCCESS;
	if((hWndListLocal == NULL) ||
	   (hWndListRemote == NULL))
	{
		Sts = FFFTP_FAIL;
	}
	return(Sts);
}


/*----- ファイルリストウインドウを削除 ----------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void DeleteListWin(void)
{
//	if(ListImg != NULL)
//		ImageList_Destroy(ListImg);
	if(hWndListLocal != NULL)
		DestroyWindow(hWndListLocal);
	if(hWndListRemote != NULL)
		DestroyWindow(hWndListRemote);
	return;
}


/*----- ローカル側のファイルリストのウインドウハンドルを返す ------------------
*
*	Parameter
*		なし
*
*	Return Value
*		HWND ウインドウハンドル
*----------------------------------------------------------------------------*/

HWND GetLocalHwnd(void)
{
	return(hWndListLocal);
}


/*----- ホスト側のファイルリストのウインドウハンドルを返す --------------------
*
*	Parameter
*		なし
*
*	Return Value
*		HWND ウインドウハンドル
*----------------------------------------------------------------------------*/

HWND GetRemoteHwnd(void)
{
	return(hWndListRemote);
}


/*----- ローカル側のファイルリストウインドウのメッセージ処理 ------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		UINT message  : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		メッセージに対応する戻り値
*----------------------------------------------------------------------------*/

static LRESULT CALLBACK LocalWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return(FileListCommonWndProc(hWnd, message, wParam, lParam));
}


/*----- ホスト側のファイルリストウインドウのメッセージ処理 --------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		UINT message  : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		メッセージに対応する戻り値
*----------------------------------------------------------------------------*/

static LRESULT CALLBACK RemoteWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return(FileListCommonWndProc(hWnd, message, wParam, lParam));
}


static void doTransferRemoteFile(void)
{
	// すでにリモートから転送済みなら何もしない。(2007.9.3 yutaka)
	if (!empty(remoteFileListBase))
		return;

	// 特定の操作を行うと異常終了するバグ修正
	while(1)
	{
		MSG msg;
		if(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		else if(AskTransferNow() == NO)
			break;
		Sleep(10);
	}

	std::vector<FILELIST> FileListBase;
	MakeSelectedFileList(WIN_REMOTE, YES, NO, FileListBase, &CancelFlg);
	std::vector<FILELIST> FileListBaseNoExpand;
	MakeSelectedFileList(WIN_REMOTE, NO, NO, FileListBaseNoExpand, &CancelFlg);

	// set temporary folder
	auto LocDir = AskLocalCurDir();

	auto tmp = tempDirectory() / L"file";
	if (auto const created = !fs::create_directory(tmp); !created) {
		// 既存のファイルを削除する
		for (auto const& f : FileListBase)
			fs::remove(tmp / f.Name);
	}

	// 外部アプリケーションへドロップ後にローカル側のファイル一覧に作業フォルダが表示されるバグ対策
	SuppressRefresh = 1;

	// ダウンロード先をテンポラリに設定
	SetLocalDirHist(tmp);

	// FFFTPにダウンロード要求を出し、ダウンロードの完了を待つ。
	PostMessageW(GetMainHwnd(), WM_COMMAND, MAKEWPARAM(MENU_DOWNLOAD, 0), 0);

	// 特定の操作を行うと異常終了するバグ修正
	while(1)
	{
		MSG msg;

		if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);

		} else {
			// 転送スレッドが動き出したら抜ける。
			if (AskTransferNow() == YES)
				break;
		}

		Sleep(10);
	}

	// 特定の操作を行うと異常終了するバグ修正
	while(1)
	{
		MSG msg;
		if(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		else if(AskTransferNow() == NO)
			break;
		Sleep(10);
	}

	// ダウンロード先を元に戻す
	SetLocalDirHist(LocDir);
	SetCurrentDirAsDirHist();

	// 外部アプリケーションへドロップ後にローカル側のファイル一覧に作業フォルダが表示されるバグ対策
	SuppressRefresh = 0;
	GetLocalDirForWnd();

	remoteFileListBase = std::move(FileListBase);
	remoteFileListBaseNoExpand = std::move(FileListBaseNoExpand);
	remoteFileDir = std::move(tmp);
}


// テンポラリのファイルおよびフォルダを削除する。
void doDeleteRemoteFile(void)
{
	if (std::error_code ec; !empty(remoteFileListBase)) {
		fs::remove_all(remoteFileDir, ec);
		remoteFileListBase.clear();
	}

	remoteFileListBaseNoExpand.clear();
}


// yutaka
// cf. http://www.nakka.com/lib/
/* ドロップファイルの作成 */
static HDROP CreateDropFileMem(std::vector<fs::path> const& filenames) {
	// 構造体の後ろに続くファイル名のリスト（ファイル名\0ファイル名\0ファイル名\0\0）
	std::wstring extra;
	for (auto const& filename : filenames) {
		extra += filename;
		extra += L'\0';
	}
	extra += L'\0';
	if (auto drop = (HDROP)GlobalAlloc(GHND, sizeof(DROPFILES) + size(extra) * sizeof(wchar_t))) {
		if (auto dropfiles = reinterpret_cast<DROPFILES*>(GlobalLock(drop))) {
			*dropfiles = { sizeof(DROPFILES), {}, false, true };
			std::copy(begin(extra), end(extra), reinterpret_cast<wchar_t*>(dropfiles + 1));
			GlobalUnlock(drop);
			return drop;
		}
		GlobalFree(drop);
	}
	return 0;
}


// OLE D&Dを開始する 
// (2007.8.30 yutaka)
static void doDragDrop(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	POINT pt;

	// テンポラリをきれいにする (2007.9.3 yutaka)
	doDeleteRemoteFile();

	/* ドラッグ&ドロップの開始 */
	CLIPFORMAT clipFormat = CF_HDROP;
	OleDragDrop::DoDragDrop(hWnd, WM_GETDATA, WM_DRAGOVER, &clipFormat, 1, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK);

	// ドロップ先のアプリに WM_LBUTTONUP を飛ばす。
	// 特定の操作を行うと異常終了するバグ修正
//	GetCursorPos(&pt);
	pt = DropPoint;
	ScreenToClient(hWnd, &pt);
	PostMessageW(hWnd,WM_LBUTTONUP,0,MAKELPARAM(pt.x,pt.y));
	// ドロップ先が他プロセスかつカーソルが自プロセスのドロップ可能なウィンドウ上にある場合の対策
	EnableWindow(GetMainHwnd(), TRUE);
}



/*----- ファイル一覧ウインドウの共通メッセージ処理 ----------------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		UINT message  : メッセージ番号
*		WPARAM wParam : メッセージの WPARAM 引数
*		LPARAM lParam : メッセージの LPARAM 引数
*
*	Return Value
*		メッセージに対応する戻り値
*----------------------------------------------------------------------------*/

static LRESULT FileListCommonWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	POINT Point;
	HWND hWndPnt;
	HWND hWndParent;
	static POINT DragPoint;
	static HWND hWndDragStart;
	static int RemoteDropFileIndex = -1;
	int Win;
	HWND hWndDst;
	WNDPROC ProcPtr;
	HWND hWndHistEdit;
	// 特定の操作を行うと異常終了するバグ修正
	static int DragFirstTime = NO;

	Win = WIN_LOCAL;
	hWndDst = hWndListRemote;
	ProcPtr = LocalProcPtr;
	hWndHistEdit = GetLocalHistEditHwnd();
	if(hWnd == hWndListRemote)
	{
		Win = WIN_REMOTE;
		hWndDst = hWndListLocal;
		ProcPtr = RemoteProcPtr;
		hWndHistEdit = GetRemoteHistEditHwnd();
	}

	switch (message)
	{
		case WM_SYSKEYDOWN:
			if (wParam == 'D') {	// Alt+D
				SetFocus(hWndHistEdit);
				break;
			}
			EraseListViewTips();
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_KEYDOWN:
			if(wParam == 0x09)
			{
				SetFocus(hWndDst);
				break;
			}
			EraseListViewTips();
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_SETFOCUS :
			SetFocusHwnd(hWnd);
			MakeButtonsFocus();
			DispCurrentWindow(Win);
			DispSelectedSpace();
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_KILLFOCUS :
			EraseListViewTips();
			MakeButtonsFocus();
			DispCurrentWindow(-1);
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_DROPFILES :
			if (!AskUserOpeDisabled())
				if (Dragging != YES) {		// ドラッグ中は処理しない。ドラッグ後にWM_LBUTTONDOWNが飛んでくるため、そこで処理する。
					if (hWnd == hWndListRemote) {
						if (AskConnecting() == YES)
							UploadDragProc(wParam);
					} else if (hWnd == hWndListLocal)
						ChangeDirDropFileProc(wParam);
				}
			DragFinish((HDROP)wParam);
			return 0;

		case WM_LBUTTONDOWN :
			if (AskUserOpeDisabled())
				break;
			if(Dragging == YES)
				break;
			DragFirstTime = NO;
			GetCursorPos(&DropPoint);
			EraseListViewTips();
			SetFocus(hWnd);
			DragPoint.x = LOWORD(lParam);
			DragPoint.y = HIWORD(lParam);
			hWndDragStart = hWnd;
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
			break;

		case WM_LBUTTONUP :
			if (AskUserOpeDisabled())
				break;
			if(Dragging == YES)
			{
				Dragging = NO;
				ReleaseCapture();

				Point.x = (long)(short)LOWORD(lParam);
				Point.y = (long)(short)HIWORD(lParam);
				ClientToScreen(hWnd, &Point);
				hWndPnt = WindowFromPoint(Point);
				if(hWndPnt == hWndDst)  // local <-> remote 
				{
					if(hWndPnt == hWndListRemote) {
						PostMessageW(GetMainHwnd(), WM_COMMAND, MAKEWPARAM(MENU_UPLOAD, 0), 0);
					} else if(hWndPnt == hWndListLocal) {
						PostMessageW(GetMainHwnd(), WM_COMMAND, MAKEWPARAM(MENU_DOWNLOAD, 0), 0);
					}
				} else { // 同一ウィンドウ内の場合 (yutaka)
					if (hWndDragStart == hWndListRemote && hWndPnt == hWndListRemote) {
						// remote <-> remoteの場合は、サーバでのファイルの移動を行う。(2007.9.5 yutaka)
						if (RemoteDropFileIndex != -1) {
							ListView_SetItemState(hWnd, RemoteDropFileIndex, 0, LVIS_DROPHILITED);
							MoveRemoteFileProc(RemoteDropFileIndex);
						}

					}

				}
			}
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		case WM_DRAGDROP:  
			// OLE D&Dを開始する (yutaka)
			// 特定の操作を行うと異常終了するバグ修正
//			doDragDrop(hWnd, message, wParam, lParam);
			if(DragFirstTime == NO)
				doDragDrop(hWnd, message, wParam, lParam);
			DragFirstTime = YES;
			return (TRUE);
			break;

		case WM_GETDATA:  // ファイルのパスをD&D先のアプリへ返す (yutaka)
			switch(wParam)
			{
			case CF_HDROP:		/* ファイル */
				{
					std::vector<FILELIST> FileListBase, FileListBaseNoExpand;
					fs::path PathDir;

					// 特定の操作を行うと異常終了するバグ修正
					GetCursorPos(&DropPoint);
					hWndPnt = WindowFromPoint(DropPoint);
					hWndParent = GetParent(hWndPnt);
					DisableUserOpe();
					CancelFlg = NO;

					// ローカル側で選ばれているファイルをFileListBaseに登録
					if (hWndDragStart == hWndListLocal) {
						PathDir = AskLocalCurDir();

						if(hWndPnt != hWndListRemote && hWndPnt != hWndListLocal && hWndParent != hWndListRemote && hWndParent != hWndListLocal)
							MakeSelectedFileList(WIN_LOCAL, NO, NO, FileListBase, &CancelFlg);			
						FileListBaseNoExpand = FileListBase;

					} else if (hWndDragStart == hWndListRemote) {
						if (hWndPnt == hWndListRemote || hWndPnt == hWndListLocal || hWndParent == hWndListRemote || hWndParent == hWndListLocal) {
						} else {
							// 選択されているリモートファイルのリストアップ
							// このタイミングでリモートからローカルの一時フォルダへダウンロードする
							// (2007.8.31 yutaka)
							doTransferRemoteFile();
							PathDir = remoteFileDir;
							FileListBase = remoteFileListBase;
							FileListBaseNoExpand = remoteFileListBaseNoExpand;
						}

					} 

					auto const& pf =
#if defined(HAVE_TANDEM)
						empty(FileListBaseNoExpand) ? FileListBase :
#endif
						FileListBaseNoExpand;
					// 特定の操作を行うと異常終了するバグ修正
					if (!empty(pf)) {
						Dragging = NO;
						ReleaseCapture();
						// ドロップ先が他プロセスかつカーソルが自プロセスのドロップ可能なウィンドウ上にある場合の対策
						EnableWindow(GetMainHwnd(), FALSE);
					}
					EnableUserOpe();

					if (empty(pf)) {
						// ファイルが未選択の場合は何もしない。(yutaka)
						*(HANDLE*)lParam = NULL;
						return FALSE;
					}
					// ドロップ先が他プロセスかつカーソルが自プロセスのドロップ可能なウィンドウ上にある場合の対策
					EnableWindow(GetMainHwnd(), FALSE);
					
					/* ドロップファイルリストの作成 */
					/* ファイル名の配列を作成する */
					/* NTの場合はUNICODEになるようにする */
					std::vector<fs::path> filenames;
					for (auto const& f : FileListBaseNoExpand)
						filenames.emplace_back(PathDir / f.Name);
					*(HANDLE*)lParam = CreateDropFileMem(filenames);
					return TRUE;
				}
				break;

			default:
				*((HANDLE *)lParam) = NULL;
				break;
			}

			break;

		case WM_DRAGOVER:
			{
				// 同一ウィンドウ内でのD&Dはリモート側のみ
				if (Win != WIN_REMOTE)
					break;
				if (MoveMode == MOVE_DISABLE)
					break;

				GetCursorPos(&Point);
				hWndPnt = WindowFromPoint(Point);
				ScreenToClient(hWnd, &Point);

				// 以前の選択を消す
				static int prev_index = -1;
				ListView_SetItemState(hWnd, prev_index, 0, LVIS_DROPHILITED);
				RemoteDropFileIndex = -1;

				if (hWndPnt == hWndListRemote)
					if (LVHITTESTINFO hi{ Point }; ListView_HitTest(hWnd, &hi) != -1 && hi.flags == LVHT_ONITEMLABEL) { // The position is over a list-view item's text.
						prev_index = hi.iItem;
						if (GetNodeType(Win, hi.iItem) == NODE_DIR) {
							ListView_SetItemState(hWnd, hi.iItem, LVIS_DROPHILITED, LVIS_DROPHILITED);
							RemoteDropFileIndex = hi.iItem;
						}
					}
			}
			break;

		case WM_RBUTTONDOWN :
			if (AskUserOpeDisabled())
				break;
			/* ここでファイルを選ぶ */
			CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

			EraseListViewTips();
			SetFocus(hWnd);
			if(hWnd == hWndListRemote)
				ShowPopupMenu(WIN_REMOTE, 0);
			else if(hWnd == hWndListLocal)
				ShowPopupMenu(WIN_LOCAL, 0);
			break;

		case WM_LBUTTONDBLCLK :
			if (AskUserOpeDisabled())
				break;
			DoubleClickProc(Win, NO, -1);
			break;

		case WM_MOUSEMOVE :
			if (AskUserOpeDisabled())
				break;
			if(wParam == MK_LBUTTON)
			{
				if((Dragging == NO) && 
				   (hWnd == hWndDragStart) &&
				   (AskConnecting() == YES) &&
				   (SendMessageW(hWnd, LVM_GETSELECTEDCOUNT, 0, 0) > 0) &&
				   ((abs((short)LOWORD(lParam) - DragPoint.x) > 5) ||
					(abs((short)HIWORD(lParam) - DragPoint.y) > 5)))
				{
					SetCapture(hWnd);
					Dragging = YES;
				}
				else if(Dragging == YES)
				{
					// OLE D&Dの開始を指示する
					PostMessageW(hWnd, WM_DRAGDROP, MAKEWPARAM(wParam, lParam), 0);

				}
				else
					return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
			}
			else
			{
				CheckTipsDisplay(hWnd, lParam);
				return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
			}
			break;

		case WM_MOUSEWHEEL :
			if (AskUserOpeDisabled())
				break;
			if(Dragging == NO)
			{
				short zDelta = (short)HIWORD(wParam);

				EraseListViewTips();
				Point.x = (short)LOWORD(lParam);
				Point.y = (short)HIWORD(lParam);
				hWndPnt = WindowFromPoint(Point);

				if((wParam & MAKEWPARAM(MK_SHIFT, 0)) && 
				   ((hWndPnt == hWndListRemote) ||
					(hWndPnt == hWndListLocal) || 
					(hWndPnt == GetTaskWnd())))
				{
					PostMessageW(hWndPnt, WM_VSCROLL, zDelta > 0 ? MAKEWPARAM(SB_PAGEUP, 0) : MAKEWPARAM(SB_PAGEDOWN, 0), 0);
				}
				else if(hWndPnt == hWnd)
					return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
				else if((hWndPnt == hWndDst) || (hWndPnt == GetTaskWnd()))
					PostMessageW(hWndPnt, message, wParam, lParam);
			}
			break;

		case WM_NOTIFY:
			switch (auto hdr = reinterpret_cast<NMHDR*>(lParam); hdr->code) {
			case HDN_ITEMCHANGEDW:
				if (auto header = reinterpret_cast<NMHEADERW*>(lParam); header->pitem && (header->pitem->mask & HDI_WIDTH))
					(hWnd == hWndListLocal ? LocalTabWidth : RemoteTabWidth)[header->iItem] = header->pitem->cxy;
				break;
			}
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);

		default :
			return CallWindowProcW(ProcPtr, hWnd, message, wParam, lParam);
	}
	return(0L);
}


/*----- ファイル一覧方法にしたがってリストビューを設定する --------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void SetListViewType(void)
{
	// 64ビット対応
//	long lStyle;
	LONG_PTR lStyle;

	switch(ListType)
	{
		case LVS_LIST :
			lStyle = GetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE, lStyle & ~LVS_REPORT | LVS_LIST);

			lStyle = GetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE, lStyle & ~LVS_REPORT | LVS_LIST);
			break;

		default :
			lStyle = GetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetLocalHwnd(), GWL_STYLE, lStyle & ~LVS_LIST | LVS_REPORT);

			lStyle = GetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE);
			SetWindowLongPtrW(GetRemoteHwnd(), GWL_STYLE, lStyle & ~LVS_LIST | LVS_REPORT);
			break;
	}
	return;
}


// ホスト側のファイル一覧ウインドウにファイル名をセット
void GetRemoteDirForWnd(int Mode, int *CancelCheckWork) {
	if (AskConnecting() == YES) {
		DisableUserOpe();
		SetRemoteDirHist(AskRemoteCurDir());
		if (Mode == CACHE_LASTREAD || DoDirList(L""sv, 0, CancelCheckWork) == FTP_COMPLETE) {
			if (auto lines = GetListLine(0)) {
				std::vector<FILELIST> files;
				for (auto const& line : *lines)
					if (auto p = std::get_if<FILELIST>(&line); p && p->Node != NODE_NONE && AskFilterStr(p->Name, p->Node) == YES && (DotFile == YES || p->Name[0] != '.'))
						files.emplace_back(*p);
				DispFileList2View(GetRemoteHwnd(), files);

				// 先頭のアイテムを選択
				ListView_SetItemState(GetRemoteHwnd(), 0, LVIS_FOCUSED, LVIS_FOCUSED);
			} else {
				SetTaskMsg(IDS_MSGJPN048);
				SendMessageW(GetRemoteHwnd(), LVM_DELETEALLITEMS, 0, 0);
			}
		} else {
#if defined(HAVE_OPENVMS)
			/* OpenVMSの場合空ディレクトリ移動の時に出るので、メッセージだけ出さない
			 * ようにする(VIEWはクリアして良い) */
			if (AskHostType() != HTYPE_VMS)
#endif
				SetTaskMsg(IDS_MSGJPN049);
			SendMessageW(GetRemoteHwnd(), LVM_DELETEALLITEMS, 0, 0);
		}
		EnableUserOpe();
	}
}


// ローカル側のファイル一覧ウインドウにファイル名をセット
void RefreshIconImageList(std::vector<FILELIST>& files)
{
	HBITMAP hBitmap;
	
	if(DispFileIcon == YES)
	{
		SendMessageW(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)NULL);
		ShowWindow(hWndListLocal, SW_SHOW);
		SendMessageW(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)NULL);
		ShowWindow(hWndListRemote, SW_SHOW);
		ImageList_Destroy(ListImgFileIcon);
		ListImgFileIcon = ImageList_Create(16, 16, ILC_MASK | ILC_COLOR32, 0, 1);
		hBitmap = LoadBitmapW(GetFtpInst(), MAKEINTRESOURCEW(dirattr16_bmp));
		ImageList_AddMasked(ListImgFileIcon, hBitmap, RGB(255, 0, 0));
		DeleteObject(hBitmap);
		int ImageId = 0;
		for (auto& file : files) {
			file.ImageId = -1;
			fs::path fullpath{ file.Name };
			if (file.Node != NODE_DRIVE)
				fullpath = fs::current_path() / fullpath;
			if (SHFILEINFOW fi; __pragma(warning(suppress:6001)) SHGetFileInfoW(fullpath.c_str(), 0, &fi, sizeof(SHFILEINFOW), SHGFI_SMALLICON | SHGFI_ICON)) {
				if (ImageList_AddIcon(ListImgFileIcon, fi.hIcon) >= 0)
					file.ImageId = ImageId++;
				DestroyIcon(fi.hIcon);
			}
		}
		SendMessageW(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImgFileIcon);
		ShowWindow(hWndListLocal, SW_SHOW);
		SendMessageW(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImgFileIcon);
		ShowWindow(hWndListRemote, SW_SHOW);
	}
	else
	{
		SendMessageW(hWndListLocal, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListLocal, SW_SHOW);
		SendMessageW(hWndListRemote, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)ListImg);
		ShowWindow(hWndListRemote, SW_SHOW);
	}
}

void GetLocalDirForWnd() {
	std::vector<FILELIST> files;
	auto const cwd = DoLocalPWD();
	SetLocalDirHist(cwd);
	DispLocalFreeSpace(cwd);

	// ローカル側自動更新
	if(ChangeNotification != INVALID_HANDLE_VALUE)
		FindCloseChangeNotification(ChangeNotification);
	ChangeNotification = FindFirstChangeNotificationW(cwd.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);

	/* ディレクトリ／ファイル */
	FindFile(cwd / L"*", [&files](WIN32_FIND_DATAW const& data) {
		if (DotFile != YES && data.cFileName[0] == L'.')
			return true;
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			files.emplace_back(data.cFileName, NODE_DIR, NO, (int64_t)data.nFileSizeHigh << 32 | data.nFileSizeLow, 0, data.ftLastWriteTime, FINFO_ALL);
		else if (AskFilterStr(data.cFileName, NODE_FILE) == YES)
			files.emplace_back(data.cFileName, NODE_FILE, NO, (int64_t)data.nFileSizeHigh << 32 | data.nFileSizeLow, 0, data.ftLastWriteTime, FINFO_ALL);
		return true;
	});

	/* ドライブ */
	if (DispDrives)
		GetDrives([&files](const wchar_t drive[]) { files.emplace_back(drive, NODE_DRIVE, NO, 0, 0, FILETIME{}, FINFO_ALL); });

	// ファイルアイコン表示対応
	RefreshIconImageList(files);
	DispFileList2View(GetLocalHwnd(), files);

	// 先頭のアイテムを選択
	ListView_SetItemState(GetLocalHwnd(), 0, LVIS_FOCUSED, LVIS_FOCUSED);

	return;
}


// ファイル一覧用リストの内容をファイル一覧ウインドウにセット
static void DispFileList2View(HWND hWnd, std::vector<FILELIST>& files) {
	std::sort(begin(files), end(files), [hWnd](FILELIST& l, FILELIST& r) {
		if (l.Node != r.Node)
			return l.Node < r.Node;
		auto Sort = AskSortType(hWnd == GetRemoteHwnd() ? l.Node == NODE_DIR ? ITEM_RDIR : ITEM_RFILE : l.Node == NODE_DIR ? ITEM_LDIR : ITEM_LFILE);
		auto test = [ascent = (Sort & SORT_GET_ORD) == SORT_ASCENT](auto r) { return ascent ? r < 0 : r > 0; };
		LONGLONG Cmp = 0;
		fs::path lf{ l.Name }, rf{ r.Name };
		if ((Sort & SORT_MASK_ORD) == SORT_EXT && test(Cmp = _wcsicmp(lf.extension().c_str(), rf.extension().c_str())))
			return true;
#if defined(HAVE_TANDEM)
		if (AskHostType() == HTYPE_TANDEM && (Sort & SORT_MASK_ORD) == SORT_EXT && test(Cmp = (LONGLONG)l.Attr - r.Attr))
			return true;
#endif
		if ((Sort & SORT_MASK_ORD) == SORT_SIZE && test(Cmp = l.Size - r.Size))
			return true;
		if ((Sort & SORT_MASK_ORD) == SORT_DATE && test(Cmp = CompareFileTime(&l.Time, &r.Time)))
			return true;
		if ((Sort & SORT_MASK_ORD) == SORT_NAME || Cmp == 0)
			if (test(_wcsicmp(lf.c_str(), rf.c_str())))
				return true;
		return false;
	});

	SendMessageW(hWnd, WM_SETREDRAW, false, 0);
	SendMessageW(hWnd, LVM_DELETEALLITEMS, 0, 0);

	for (auto& file : files)
		AddListView(hWnd, -1, file.Name, file.Node, file.Size, &file.Time, file.Attr, file.Owner, file.Link, file.InfoExist, file.ImageId);

	SendMessageW(hWnd, WM_SETREDRAW, true, 0);
	UpdateWindow(hWnd);

	DispSelectedSpace();
}

// FILETIME(UTC)を日付文字列(JST)に変換
static auto FileTimeToString(FILETIME ft, int InfoExist) {
	std::wstring str;
	if ((ft.dwLowDateTime != 0 || ft.dwHighDateTime != 0) && (InfoExist & (FINFO_DATE | FINFO_TIME)) != 0) {
		FileTimeToLocalFileTime(&ft, &ft);
		if (SYSTEMTIME st; FileTimeToSystemTime(&ft, &st)) {
			if (InfoExist & FINFO_DATE)
				std::format_to(std::back_inserter(str), L"{:04d}/{:02d}/{:02d} "sv, st.wYear, st.wMonth, st.wDay);
			else
				str += L"           "sv;
			if (InfoExist & FINFO_TIME)
				std::format_to(std::back_inserter(str), DispTimeSeconds == YES ? L"{:02d}:{:02d}:{:02d}"sv : L"{:02d}:{:02d}"sv, st.wHour, st.wMinute, st.wSecond);
			else
				str += DispTimeSeconds == YES ? L"        "sv : L"     "sv;
		}
	}
	return str;
}

// パス名の中の拡張子の先頭を返す
static std::wstring GetFileExt(std::wstring const& path) {
	if (path != L"."sv && path != L".."sv)
		if (auto const pos = path.rfind(L'.'); pos != std::wstring::npos)
			return path.substr(pos + 1);
	return {};
}

// 属性の値を文字列に変換
static std::wstring AttrValue2String(int Attr) {
	if (DispPermissionsNumber == YES)
		return std::format(L"{:03x}"sv, Attr);
	auto str = L"rwxrwxrwx"s;
	constexpr int masks[] = { 0x400, 0x200, 0x100, 0x40, 0x20, 0x10, 0x4, 0x2, 0x1 };
	for (size_t i = 0; i < std::size(masks); i++)
		if ((Attr & masks[i]) == 0)
			str[i] = L'-';
	return str;
}

/*----- ファイル一覧ウインドウ（リストビュー）に追加 --------------------------
*
*	Parameter
*		HWND hWnd : ウインドウハンドル
*		int Pos : 挿入位置
*		char *Name : 名前
*		int Type : タイプ (NIDE_xxxx)
*		LONGLONG Size : サイズ
*		FILETIME *Time : 日付
*		int Attr : 属性
*		char Owner : オーナ名
*		int Link : リンクかどうか
*		int InfoExist : 情報があるかどうか (FINFO_xxx)
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/
static void AddListView(HWND hWnd, int Pos, std::wstring const& Name, int Type, LONGLONG Size, FILETIME* Time, int Attr, std::wstring const& Owner, int Link, int InfoExist, int ImageId) {
	static const std::locale default_locale{ ""s };
	LVITEMW item;
	std::wstring text;

	/* アイコン/ファイル名 */
	if (Pos == -1)
		Pos = std::numeric_limits<int>::max();
	if (Type == NODE_FILE && AskTransferTypeAssoc(Name, TYPE_X) == TYPE_I)
		Type = 3;
	item = { .mask = LVIF_TEXT | LVIF_IMAGE, .iItem = Pos, .pszText = const_cast<LPWSTR>(Name.c_str()), .iImage = DispFileIcon == YES && hWnd == GetLocalHwnd() ? ImageId + 5 : Link == NO ? Type : 4 };
	Pos = (int)SendMessageW(hWnd, LVM_INSERTITEMW, 0, (LPARAM)&item);

	/* 日付/時刻 */
	text = FileTimeToString(*Time, InfoExist);
	item = { .mask = LVIF_TEXT, .iItem = Pos, .iSubItem = 1, .pszText = const_cast<LPWSTR>(text.c_str()) };
	SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&item);

	/* サイズ */
	text = Type == NODE_DIR ? L"<DIR>"s
		: Type == NODE_DRIVE ? L"<DRIVE>"s
		: 0 <= Size ? std::format(default_locale, L"{:Ld}"sv, Size)
		: L""s;
	item = { .mask = LVIF_TEXT, .iItem = Pos, .iSubItem = 2, .pszText = const_cast<LPWSTR>(text.c_str()) };
	SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&item);

	/* 拡張子 */
	std::wstring extension;
#if defined(HAVE_TANDEM)
	if (AskHostType() == HTYPE_TANDEM)
		extension = std::to_wstring(Attr);
	else
#endif
		extension = GetFileExt(Name);
	item = { .mask = LVIF_TEXT, .iItem = Pos, .iSubItem = 3, .pszText = const_cast<LPWSTR>(extension.c_str()) };
	SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&item);

	if (hWnd == GetRemoteHwnd()) {
		/* 属性 */
#if defined(HAVE_TANDEM)
		if ((InfoExist & FINFO_ATTR) && (AskHostType() != HTYPE_TANDEM))
#else
		if (InfoExist & FINFO_ATTR)
#endif
			text = AttrValue2String(Attr);
		else
			text = L""s;
		item = { .mask = LVIF_TEXT, .iItem = Pos, .iSubItem = 4, .pszText = const_cast<LPWSTR>(text.c_str()) };
		SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&item);

		/* オーナ名 */
		item = { .mask = LVIF_TEXT, .iItem = Pos, .iSubItem = 5, .pszText = const_cast<LPWSTR>(Owner.c_str()) };
		SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&item);
	}
}


/*----- ファイル名一覧ウインドウをソートし直す --------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void ReSortDispList(int Win, int *CancelCheckWork)
{
	if(Win == WIN_REMOTE)
		GetRemoteDirForWnd(CACHE_LASTREAD, CancelCheckWork);
	else
		GetLocalDirForWnd();
	return;
}


// ワイルドカードにマッチするかどうかを返す
//   VAX VMSの時は ; 以降は無視する
bool CheckFname(std::wstring const& file, std::wstring const& spec) {
	auto const pos = AskHostType() == HTYPE_VMS ? file.find(L';') : std::wstring::npos;
	return PathMatchSpecW((pos == std::wstring::npos ? file : file.substr(0, pos)).c_str(), spec.c_str());
}


// ファイル一覧ウインドウのファイルを選択する
void SelectFileInList(HWND hWnd, int Type, std::vector<FILELIST> const& Base) {
	static bool IgnoreNew = false;
	static bool IgnoreOld = false;
	static bool IgnoreExist = false;
	struct Select {
		using result_t = bool;
		INT_PTR OnInit(HWND hDlg) {
			SendDlgItemMessageW(hDlg, SEL_FNAME, EM_LIMITTEXT, 40, 0);
			SetText(hDlg, SEL_FNAME, FindStr);
			SendDlgItemMessageW(hDlg, SEL_REGEXP, BM_SETCHECK, FindMode, 0);
			SendDlgItemMessageW(hDlg, SEL_NOOLD, BM_SETCHECK, IgnoreOld ? BST_CHECKED : BST_UNCHECKED, 0);
			SendDlgItemMessageW(hDlg, SEL_NONEW, BM_SETCHECK, IgnoreNew ? BST_CHECKED : BST_UNCHECKED, 0);
			SendDlgItemMessageW(hDlg, SEL_NOEXIST, BM_SETCHECK, IgnoreExist ? BST_CHECKED : BST_UNCHECKED, 0);
			return TRUE;
		}
		void OnCommand(HWND hDlg, WORD id) {
			switch (id) {
			case IDOK:
				FindStr = GetText(hDlg, SEL_FNAME);
				FindMode = (int)SendDlgItemMessageW(hDlg, SEL_REGEXP, BM_GETCHECK, 0, 0);
				IgnoreOld = SendDlgItemMessageW(hDlg, SEL_NOOLD, BM_GETCHECK, 0, 0) == BST_CHECKED;
				IgnoreNew = SendDlgItemMessageW(hDlg, SEL_NONEW, BM_GETCHECK, 0, 0) == BST_CHECKED;
				IgnoreExist = SendDlgItemMessageW(hDlg, SEL_NOEXIST, BM_GETCHECK, 0, 0) == BST_CHECKED;
				EndDialog(hDlg, true);
				break;
			case IDCANCEL:
				EndDialog(hDlg, false);
				break;
			case IDHELP:
				ShowHelp(IDH_HELP_TOPIC_0000061);
				break;
			}
		}
	};
	int Win = WIN_LOCAL, WinDst = WIN_REMOTE;
	if (hWnd == GetRemoteHwnd())
		std::swap(Win, WinDst);
	if (Type == SELECT_ALL) {
		LVITEMW item{ 0, 0, 0, GetSelectedCount(Win) <= 1 ? LVIS_SELECTED : 0u, LVIS_SELECTED };
		for (int i = 0, Num = GetItemCount(Win); i < Num; i++)
			if (GetNodeType(Win, i) != NODE_DRIVE)
				SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
		return;
	}
	if (Type == SELECT_REGEXP) {
		if (!Dialog(GetFtpInst(), Win == WIN_LOCAL ? sel_local_dlg : sel_remote_dlg, hWnd, Select{}))
			return;
		try {
			std::variant<std::wstring, boost::wregex> pattern;
			if (FindMode == 0)
				pattern = FindStr;
			else
				pattern = boost::wregex{ FindStr, boost::regex_constants::icase };
			int CsrPos = -1;
			for (int i = 0, Num = GetItemCount(Win); i < Num; i++) {
				auto const name = GetNodeName(Win, i);
				int Find = FindNameNode(WinDst, name);
				UINT state = 0;
				if (GetNodeType(Win, i) != NODE_DRIVE) {
					auto matched = std::visit([&name](auto&& pattern) {
						using t = std::decay_t<decltype(pattern)>;
						if constexpr (std::is_same_v<t, std::wstring>)
							return CheckFname(name, pattern);
						else if constexpr (std::is_same_v<t, boost::wregex>)
							return boost::regex_match(name, pattern);
						else
							static_assert(false_v<t>, "not supported variant type.");
					}, pattern);
					if (matched) {
						state = LVIS_SELECTED;
						if (Find >= 0) {
							if (IgnoreExist)
								state = 0;
							else {
								FILETIME Time1, Time2;
								GetNodeTime(Win, i, Time1);
								GetNodeTime(WinDst, Find, Time2);
								if (IgnoreNew && CompareFileTime(&Time1, &Time2) > 0 || IgnoreOld && CompareFileTime(&Time1, &Time2) < 0)
									state = 0;
							}
						}
					}
					if (state != 0 && CsrPos == -1)
						CsrPos = i;
				}
				LVITEMW item{ 0, 0, 0, state, LVIS_SELECTED };
				SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
			}
			if (CsrPos != -1) {
				LVITEMW item{ 0, 0, 0, LVIS_FOCUSED, LVIS_FOCUSED };
				SendMessageW(hWnd, LVM_SETITEMSTATE, CsrPos, (LPARAM)&item);
				SendMessageW(hWnd, LVM_ENSUREVISIBLE, CsrPos, (LPARAM)TRUE);
			}
		}
		catch (boost::regex_error&) {}
		return;
	}
	if (Type == SELECT_LIST) {
		for (int i = 0, Num = GetItemCount(Win); i < Num; i++) {
			auto const name = GetNodeName(Win, i);
			LVITEMW item{ 0, 0, 0, SearchFileList(name, Base, COMP_STRICT) != NULL ? LVIS_SELECTED : 0u, LVIS_SELECTED };
			SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
		}
		return;
	}
}


// ファイル一覧ウインドウのファイルを検索する
void FindFileInList(HWND hWnd, int Type) {
	static std::variant<std::wstring, boost::wregex> pattern;
	int Win = hWnd == GetRemoteHwnd() ? WIN_REMOTE : WIN_LOCAL;
	switch (Type) {
	case FIND_FIRST:
		if (!InputDialog(find_dlg, hWnd, Win == WIN_LOCAL ? IDS_MSGJPN050 : IDS_MSGJPN051, FindStr, 40 + 1, &FindMode))
			return;
		try {
			if (FindMode == 0)
				pattern = FindStr;
			else
				pattern = boost::wregex{ FindStr, boost::regex_constants::icase };
		}
		catch (boost::regex_error&) {
			return;
		}
		[[fallthrough]];
	case FIND_NEXT:
		for (int i = GetCurrentItem(Win) + 1, Num = GetItemCount(Win); i < Num; i++) {
			auto match = std::visit([name = GetNodeName(Win, i)](auto&& pattern) {
				using t = std::decay_t<decltype(pattern)>;
				if constexpr (std::is_same_v<t, std::wstring>)
					return CheckFname(name, pattern);
				else if constexpr (std::is_same_v<t, boost::wregex>)
					return boost::regex_match(name, pattern);
				else
					static_assert(false_v<t>, "not supported variant type.");
			}, pattern);
			if (match) {
				LVITEMW item{ 0, 0, 0, LVIS_FOCUSED, LVIS_FOCUSED };
				SendMessageW(hWnd, LVM_SETITEMSTATE, i, (LPARAM)&item);
				SendMessageW(hWnd, LVM_ENSUREVISIBLE, i, (LPARAM)TRUE);
				break;
			}
		}
		break;
	}
}


/*----- カーソル位置のアイテム番号を返す --------------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		int アイテム番号
*----------------------------------------------------------------------------*/

int GetCurrentItem(int Win) {
	auto Ret = (int)SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_GETNEXTITEM, -1, LVNI_ALL | LVNI_FOCUSED);
	return Ret == -1 ? 0 : Ret;
}


/*----- アイテム数を返す ------------------------------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		int アイテム数
*----------------------------------------------------------------------------*/

int GetItemCount(int Win)
{
	HWND hWnd;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	return (int)(SendMessageW(hWnd, LVM_GETITEMCOUNT, 0, 0));
}


/*----- 選択されているアイテム数を返す ----------------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*
*	Return Value
*		int 選択されているアイテム数
*----------------------------------------------------------------------------*/

int GetSelectedCount(int Win)
{
	HWND hWnd;

	hWnd = GetLocalHwnd();
	if(Win == WIN_REMOTE)
		hWnd = GetRemoteHwnd();

	return (int)(SendMessageW(hWnd, LVM_GETSELECTEDCOUNT, 0, 0));
}


/*----- 選択されている最初のアイテム番号を返す --------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*		int All : 選ばれていないものを含める
*
*	Return Value
*		int アイテム番号
*			-1 = 選択されていない
*----------------------------------------------------------------------------*/

int GetFirstSelected(int Win, int All) {
	return GetNextSelected(Win, -1, All);
}


/*----- 選択されている次のアイテム番号を返す ----------------------------------
*
*	Parameter
*		int Win : ウィンドウ番号 (WIN_xxxx)
*		int Pos : 今のアイテム番号
*		int All : 選ばれていないものも含める
*
*	Return Value
*		int アイテム番号
*			-1 = 選択されていない
*----------------------------------------------------------------------------*/

int GetNextSelected(int Win, int Pos, int All) {
	return (int)SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_GETNEXTITEM, Pos, All == YES ? LVNI_ALL : LVNI_SELECTED);
}


std::wstring GetHotSelected(int Win) {
	auto index = (int)SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_GETNEXTITEM, -1, LVNI_FOCUSED);
	return index != -1 ? GetNodeName(Win, index) : L""s;
}


void SetHotSelected(int Win, std::wstring const& name) {
	if (auto index = FindNameNode(Win, name); index != -1) {
		LVITEMW item{ .state = LVIS_FOCUSED, .stateMask = LVIS_FOCUSED };
		SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_SETITEMSTATE, index, (LPARAM)&item);
	}
}


// 指定された名前のアイテムを探す
//   -1=見つからなかった
static int FindNameNode(int Win, std::wstring const& name) {
	LVFINDINFOW fi{ LVFI_STRING, name.c_str() };
	return (int)SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_FINDITEMW, -1, (LPARAM)&fi);
}


static std::wstring GetItemText(int Win, int index, int subitem) {
	wchar_t buffer[260 + 1];
	LVITEMW item{ .iSubItem = subitem, .pszText = buffer, .cchTextMax = size_as<int>(buffer) };
	auto length = SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_GETITEMTEXTW, index, (LPARAM)&item);
	return { buffer, (size_t)length };
}

// 指定位置のアイテムの名前を返す
std::wstring GetNodeName(int Win, int Pos) {
	return GetItemText(Win, Pos, 0);
}


// 指定位置のアイテムのUTC日付を返す
static bool GetNodeTime(int Win, int Pos, FILETIME& ft) {
	// 0123456789012345678
	// 2021/05/30 10:10:10
	// 時刻は0始まりでない場合があり、数値確認は12文字目で行う。
	// 秒が含まれない場合があり、長さは16文字以上とする。
	if (auto const text = GetItemText(Win, Pos, 1); 16 <= size(text) && iswdigit(text[0]) && iswdigit(text[5]) && iswdigit(text[8]) && iswdigit(text[12]) && iswdigit(text[14])) {
		SYSTEMTIME st{
			.wYear = WORD(_wtoi(&text[0])),
			.wMonth = WORD(_wtoi(&text[5])),
			.wDay = WORD(_wtoi(&text[8])),
			.wHour = WORD(_wtoi(&text[11])),
			.wMinute = WORD(_wtoi(&text[14])),
			.wSecond = WORD(19 <= size(text) ? _wtoi(&text[17]) : 0),
		};
		FILETIME lt;
		SystemTimeToFileTime(&st, &lt);
		LocalFileTimeToFileTime(&lt, &ft);
		return true;
	}
	return false;
}


/*----- 指定位置のアイテムのサイズを返す --------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*		int *Buf : サイズを返すワーク
*
*	Return Value
*		int ステータス
*			YES/NO=サイズ情報がなかった
*----------------------------------------------------------------------------*/

int GetNodeSize(int Win, int Pos, LONGLONG* Buf) {
	if (auto size = GetItemText(Win, Pos, 2); !empty(size)) {
		size.erase(std::remove(begin(size), end(size), L','), end(size));
		*Buf = !empty(size) && std::iswdigit(size[0]) ? stoll(size) : 0;
		return YES;
	} else {
		*Buf = -1;
		return NO;
	}
}


// 指定位置のアイテムの属性を返す
int GetNodeAttr(int Win, int Pos, int* Buf) {
	if (Win == WIN_REMOTE) {
		auto subitem = 4;
#if defined(HAVE_TANDEM)
		if (AskHostType() == HTYPE_TANDEM)
			subitem = 3;
#endif
		if (auto const attr = GetItemText(WIN_REMOTE, Pos, subitem); !empty(attr)) {
#if defined(HAVE_TANDEM)
			if (AskHostType() == HTYPE_TANDEM)
				*Buf = std::iswdigit(attr[0]) ? stoi(attr) : 0;
			else
#endif
			if (9 <= size(attr)) {
				auto value = 0;
				for (auto it = begin(attr); auto bit : { 0x400, 0x200, 0x100, 0x40, 0x20, 0x10, 0x4, 0x2, 0x1 })
					if (*it++ != L'-')
						value |= bit;
				*Buf = value;
			} else if (3 <= size(attr))
				*Buf = std::stoi(attr, nullptr, 16);
			return YES;
		}
	}
	*Buf = 0;
	return NO;
}


/*----- 指定位置のアイテムのタイプを返す --------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*
*	Return Value
*		int タイプ (NODE_xxx)
*----------------------------------------------------------------------------*/

int GetNodeType(int Win, int Pos) {
	auto type = GetItemText(Win, Pos, 2);
	return type == L"<DIR>"sv ? NODE_DIR : type == L"<DRIVE>"sv ? NODE_DRIVE : NODE_FILE;
}


/*----- 指定位置のアイテムのイメージ番号を返す ----------------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Pos : 位置
*
*	Return Value
*		int イメージ番号
*			4 Symlink
*----------------------------------------------------------------------------*/
static int GetImageIndex(int Win, int Pos) {
	LVITEMW item{ .mask = LVIF_IMAGE, .iItem = Pos, .iSubItem = 0 };
	SendMessageW(Win == WIN_REMOTE ? GetRemoteHwnd() : GetLocalHwnd(), LVM_GETITEMW, 0, (LPARAM)&item);
	return item.iImage;
}


/*----- ホスト側のファイル一覧ウインドウをクリア ------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void EraseRemoteDirForWnd(void)
{
	SendMessageW(GetRemoteHwnd(), LVM_DELETEALLITEMS, 0, 0);
	SendMessageW(GetRemoteHistHwnd(), CB_RESETCONTENT, 0, 0);
	return;
}


/*----- 選択されているファイルの総サイズを返す --------------------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*
*	Return Value
*		double サイズ
*----------------------------------------------------------------------------*/

double GetSelectedTotalSize(int Win)
{
	double Ret;
	LONGLONG Size;
	int Pos;

	Ret = 0;
	if(GetSelectedCount(Win) > 0)
	{
		Pos = GetFirstSelected(Win, NO);
		while(Pos != -1)
		{
			GetNodeSize(Win, Pos, &Size);
			if(Size >= 0)
				Ret += Size;
			Pos = GetNextSelected(Win, Pos, NO);
		}
	}
	return(Ret);
}



/*===================================================================

===================================================================*/



/*----- ファイル一覧で選ばれているファイルをリストに登録する ------------------
*
*	Parameter
*		int Win : ウインドウ番号 (WIN_xxx)
*		int Expand : サブディレクトリを展開する (YES/NO)
*		int All : 選ばれていないものもすべて登録する (YES/NO)
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

int MakeSelectedFileList(int Win, int Expand, int All, std::vector<FILELIST>& Base, int *CancelCheckWork) {
	int Sts;
	int Pos;
	int Node;
	int Ignore;

	// ファイル一覧バグ修正
	Sts = FFFTP_SUCCESS;
	if((All == YES) || (GetSelectedCount(Win) > 0))
	{
		/*===== カレントディレクトリのファイル =====*/

		Pos = GetFirstSelected(Win, All);
		while(Pos != -1)
		{
			Node = GetNodeType(Win, Pos);
			if((Node == NODE_FILE) ||
			   ((Expand == NO) && (Node == NODE_DIR)))
			{
				FILELIST Pkt{};
				Pkt.InfoExist = 0;
				Pkt.Name = GetNodeName(Win, Pos);
				if(GetNodeSize(Win, Pos, &Pkt.Size) == YES)
					Pkt.InfoExist |= FINFO_SIZE;
				if(GetNodeAttr(Win, Pos, &Pkt.Attr) == YES)
					Pkt.InfoExist |= FINFO_ATTR;
				if (GetNodeTime(Win, Pos, Pkt.Time))
					Pkt.InfoExist |= (FINFO_TIME | FINFO_DATE);
				Pkt.Node = Node;

				Ignore = NO;
				if((DispIgnoreHide == YES) && (Win == WIN_LOCAL))
					if (auto attr = GetFileAttributesW((AskLocalCurDir() / Pkt.Name).c_str()); attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN))
						Ignore = YES;

				if(Ignore == NO)
					AddFileList(Pkt, Base);
			}
			Pos = GetNextSelected(Win, Pos, All);
		}

		if(Expand == YES)
		{
			/*===== ディレクトリツリー =====*/

			Pos = GetFirstSelected(Win, All);
			while(Pos != -1)
			{
				if(GetNodeType(Win, Pos) == NODE_DIR)
				{
					FILELIST Pkt{};
					auto name = GetNodeName(Win, Pos);
					Pkt.Name = ReplaceAll(std::wstring{ name }, L'\\', L'/');
//8/26

					Ignore = NO;
					if((DispIgnoreHide == YES) && (Win == WIN_LOCAL))
						if (auto attr = GetFileAttributesW((AskLocalCurDir() / Pkt.Name).c_str()); attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN))
							Ignore = YES;

					if(Ignore == NO)
					{
//						Pkt.Node = NODE_DIR;
						if(GetImageIndex(Win, Pos) == 4) // symlink
							Pkt.Node = NODE_FILE;
						else
							Pkt.Node = NODE_DIR;
						AddFileList(Pkt, Base);

						if(GetImageIndex(Win, Pos) != 4) { // symlink
							if(Win == WIN_LOCAL)
							{
								if(!MakeLocalTree(name, Base))
									Sts = FFFTP_FAIL;
							}
							else
							{
								auto const Cur = AskRemoteCurDir();

								if((AskListCmdMode() == NO) &&
								   (AskUseNLST_R() == YES))
								{
									if(MakeRemoteTree1(name, Cur, Base, CancelCheckWork) == FFFTP_FAIL)
										Sts = FFFTP_FAIL;
								}
								else
								{
									if(MakeRemoteTree2(name, Cur, Base, CancelCheckWork) == FFFTP_FAIL)
										Sts = FFFTP_FAIL;
								}
							}
						}
					}
				}
				Pos = GetNextSelected(Win, Pos, All);
			}
		}
	}
	// ファイル一覧バグ修正
//	return;
	return(Sts);
}


static inline fs::path DragFile(HDROP hdrop, UINT index) {
	auto const length1 = DragQueryFileW(hdrop, index, nullptr, 0);
	std::wstring buffer(length1, L'\0');
	auto const length2 = DragQueryFileW(hdrop, index, data(buffer), length1 + 1);
	assert(length1 == length2);
	return std::move(buffer);
}


// Drag&Dropされたファイルをリストに登録する
std::tuple<fs::path, std::vector<FILELIST>> MakeDroppedFileList(WPARAM wParam) {
	int count = DragQueryFileW((HDROP)wParam, 0xFFFFFFFF, NULL, 0);
	auto const baseDirectory = DragFile((HDROP)wParam, 0).parent_path();
	std::vector<FILELIST> files;
	std::vector<fs::path> directories;
	for (int i = 0; i < count; i++) {
		auto const path = DragFile((HDROP)wParam, i);
		WIN32_FILE_ATTRIBUTE_DATA attr;
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
			continue;
		if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			directories.emplace_back(path);
		else {
			FILELIST Pkt{};
			Pkt.Node = NODE_FILE;
			Pkt.Name = path.filename();
			if (SYSTEMTIME TmpStime; FileTimeToSystemTime(&attr.ftLastWriteTime, &TmpStime)) {
				if (DispTimeSeconds == NO)
					TmpStime.wSecond = 0;
				TmpStime.wMilliseconds = 0;
				SystemTimeToFileTime(&TmpStime, &Pkt.Time);
			}
#if defined(HAVE_TANDEM)
			Pkt.Size = LONGLONG(attr.nFileSizeHigh) << 32 | attr.nFileSizeLow;
			Pkt.InfoExist = FINFO_TIME | FINFO_DATE | FINFO_SIZE;
#endif
			AddFileList(Pkt, files);
		}
	}

	auto const saved = fs::current_path();
	std::error_code ec;
	fs::current_path(baseDirectory, ec);
	for (auto const& path : directories) {
		FILELIST Pkt{};
		Pkt.Node = NODE_DIR;
		Pkt.Name = path.filename();
		AddFileList(Pkt, files);
		MakeLocalTree(Pkt.Name, files);
	}
	fs::current_path(saved);

	return { std::move(baseDirectory), std::move(files) };
}


// Drag&Dropされたファイルがあるフォルダを取得する
fs::path MakeDroppedDir(WPARAM wParam) {
	return DragFile((HDROP)wParam, 0).parent_path();
}


#if defined(HAVE_OPENVMS)
// VMSの"HOGE.DIR;?"というディレクトリ名から"HOGE"を取り出す
std::wstring ReformVMSDirName(std::wstring&& dirName) {
	static boost::wregex re{ LR"(\.DIR[^.]*$)" };
	/* ';'がない場合はVMS形式じゃなさそうなので何もしない */
	/* ".DIR"があったらつぶす */
	if (boost::wsmatch m; dirName.find(L';') != std::wstring::npos && boost::regex_search(dirName, m, re))
		dirName.erase(m[0].first, end(dirName));
	return dirName;
}
#endif


/*----- ホスト側のサブディレクトリ以下のファイルをリストに登録する（１）-------
*
*	Parameter
*		char *Path : パス名
*		char *Cur : カレントディレクトリ
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*
*	Note
*		NLST -alLR を使う
*----------------------------------------------------------------------------*/

static int MakeRemoteTree1(std::wstring const& Path, std::wstring const& Cur, std::vector<FILELIST>& Base, int *CancelCheckWork) {
	int Ret;
	int Sts;

	// ファイル一覧バグ修正
	Ret = FFFTP_FAIL;
	if(DoCWD(Path, NO, NO, NO) == FTP_COMPLETE)
	{
		/* サブフォルダも含めたリストを取得 */
		Sts = DoDirList(L"R"sv, 999, CancelCheckWork);	/* NLST -alLR*/
		DoCWD(Cur, NO, NO, NO);

		if(Sts == FTP_COMPLETE)
		{
			AddRemoteTreeToFileList(999, Path, RDIR_NLST, Base);
			Ret = FFFTP_SUCCESS;
		}
	}
	return(Ret);
}


/*----- ホスト側のサブディレクトリ以下のファイルをリストに登録する（２）-------
*
*	Parameter
*		char *Path : パス名
*		char *Cur : カレントディレクトリ
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*
*	Note
*		各フォルダに移動してリストを取得
*----------------------------------------------------------------------------*/

static int MakeRemoteTree2(std::wstring& Path, std::wstring const& Cur, std::vector<FILELIST>& Base, int *CancelCheckWork) {
	int Ret;
	int Sts;

	// ファイル一覧バグ修正
	Ret = FFFTP_FAIL;
	/* VAX VMS は CWD xxx/yyy という指定ができないので	*/
	/* CWD xxx, Cwd yyy と複数に分ける					*/
	if(AskHostType() != HTYPE_VMS)
		Sts = DoCWD(Path, NO, NO, NO);
	else
	{
#if defined(HAVE_OPENVMS)
		/* OpenVMSの場合、ディレクトリ移動時は"HOGE.DIR;1"を"HOGE"にする */
		Path = ReformVMSDirName(std::move(Path));
#endif
		Sts = DoCWDStepByStep(Path, Cur);
	}

	if(Sts == FTP_COMPLETE)
	{
		Sts = DoDirList(L""sv, 999, CancelCheckWork);		/* NLST -alL*/
		DoCWD(Cur, NO, NO, NO);

		if(Sts == FTP_COMPLETE)
		{
			std::vector<FILELIST> CurList;
			AddRemoteTreeToFileList(999, Path, RDIR_CWD, CurList);
			CopyTmpListToFileList(Base, CurList);

			// ファイル一覧バグ修正
			Ret = FFFTP_SUCCESS;

			for (auto& f : CurList)
				if (f.Node == NODE_DIR) {
					FILELIST Pkt{};
					/* まずディレクトリ名をセット */
					Pkt.Name = f.Name;
					Pkt.Link = f.Link;
					if (Pkt.Link == YES)
						Pkt.Node = NODE_FILE;
					else
						Pkt.Node = NODE_DIR;
					AddFileList(Pkt, Base);

					if (Pkt.Link == NO && MakeRemoteTree2(f.Name, Cur, Base, CancelCheckWork) == FFFTP_FAIL)
						Ret = FFFTP_FAIL;
				}
		}
	}
	return(Ret);
}


/*----- ファイルリストの内容を別のファイルリストにコピー ----------------------
*
*	Parameter
*		FILELIST **Base : コピー先
*		FILELIST *List : コピー元
*
*	Return Value
*		なし
*
*	Note
*		コピーするのはファイルの情報だけ
*		ディレクトリの情報はコピーしない
*----------------------------------------------------------------------------*/

static void CopyTmpListToFileList(std::vector<FILELIST>& Base, std::vector<FILELIST> const& List) {
	for (auto& f : List)
		if (f.Node == NODE_FILE)
			AddFileList(f, Base);
}


// ホスト側のファイル情報をファイルリストに登録
void AddRemoteTreeToFileList(int Num, std::wstring const& Path, int IncDir, std::vector<FILELIST>& Base) {
	auto Dir = Path;
	if (auto lines = GetListLine(Num))
		for (auto const& line : *lines)
			if (auto p = std::get_if<std::string>(&line)) {
				if (p->ends_with(':')) {		/* 最後が : ならサブディレクトリ */
					if (*p != ".:"sv) {
						std::string_view str{ *p };
						if (str.starts_with("./"sv) || str.starts_with(".\\"sv))
							str = str.substr(2);
						if (1 < size(str))
							Dir = ReplaceAll(SetSlashTail(std::wstring{ Path }) + ConvertFrom(str, CurHost.CurNameKanjiCode), L'\\', L'/');
					}
					if (IncDir == RDIR_NLST)
						AddFileList({ Dir, NODE_DIR }, Base);
				}
			} else {
				if (auto& item = std::get<FILELIST>(line); AskFilterStr(item.Name, item.Node) == YES && (item.Node == NODE_FILE || IncDir == RDIR_CWD && item.Node == NODE_DIR)) {
					FILELIST Pkt{ Dir, item.Node, item.Link, item.Size, item.Attr, item.Time, item.InfoExist };
					Pkt.Name = (empty(Pkt.Name) ? L""s : SetSlashTail(std::move(Pkt.Name))) + item.Name;
					AddFileList(Pkt, Base);
				}
			}
}

namespace re {
	static auto compile(std::tuple<std::string_view, bool> const& p) {
		auto [pattern, icase] = p;
		return icase ? boost::regex{ data(pattern), data(pattern) + size(pattern), boost::regex::icase } : boost::regex{ data(pattern), data(pattern) + size(pattern) };
	}
	static auto const mlsd      = compile(filelistparser::mlsd);
	static auto const unix      = compile(filelistparser::unix);
	static auto const linux     = compile(filelistparser::linux);
	static auto const dos       = compile(filelistparser::dos);
	static auto const melcom80  = compile(filelistparser::melcom80);
	static auto const agilent   = compile(filelistparser::agilent);
	static auto const as400     = compile(filelistparser::as400);
	static auto const m1800     = compile(filelistparser::m1800);
	static auto const gp6000    = compile(filelistparser::gp6000);
	static auto const chameleon = compile(filelistparser::chameleon);
	static auto const os2       = compile(filelistparser::os2);
	static auto const os7       = compile(filelistparser::os7);
	static auto const os9       = compile(filelistparser::os9);
	static auto const allied    = compile(filelistparser::allied);
	static auto const ibm       = compile(filelistparser::ibm);
	static auto const shibasoku = compile(filelistparser::shibasoku);
	static auto const stratus   = compile(filelistparser::stratus);
	static auto const vms       = compile(filelistparser::vms);
	static auto const irmx      = compile(filelistparser::irmx);
	static auto const tandem    = compile(filelistparser::tandem);
}

template<class Int>
static inline Int parse(std::string_view sv) {
	Int value = 0;
	std::from_chars(data(sv), data(sv) + size(sv), value);
	return value;
}

template<class Int>
static inline Int parse(boost::ssub_match const& sm) {
	return parse<Int>(sv(sm));
}

static inline WORD parseyear(boost::ssub_match const& sm) {
	auto year = parse<WORD>(sm);
	return year < 70 ? 2000 + year : year < 1601 ? 1900 + year : year;
}

static inline WORD parsemonth(boost::ssub_match const& sm) {
	if (sm.length() != 3)
		return parse<WORD>(sm);
	auto it = sm.begin();
	char name[] = { (char)toupper(*it++), (char)tolower(*it++), (char)tolower(*it++) };
	auto i = "JanFebMarAprMayJunJulAugSepOctNovDec"sv.find({ name, 3 });
	return (WORD)i / 3 + 1;
}

static int parseattr(boost::ssub_match const& m) {
	int attr = 0;
	auto it = m.begin();
	for (auto mask : { 0x400, 0x200, 0x100, 0x40, 0x20, 0x10, 0x4, 0x2, 0x1 }) {
		if (*it++ != '-')
			attr |= mask;
		if (it == m.end())
			break;
	}
	return attr;
}


static inline FILETIME tofiletime(SYSTEMTIME const& systemTime, bool fix = false) {
	FILETIME fileTime;
	if (fix) {
		TIME_ZONE_INFORMATION tz{ AskHostTimeZone() * -60 };
		SYSTEMTIME fixed;
		TzSpecificLocalTimeToSystemTime(&tz, &systemTime, &fixed);
		SystemTimeToFileTime(&fixed, &fileTime);
	} else
		SystemTimeToFileTime(&systemTime, &fileTime);
	return fileTime;
}

static std::optional<FILELIST> ParseMlsd(boost::smatch const& m) {
	char type = NODE_NONE;
	int64_t size = 0;
	int attr = 0;
	FILETIME fileTime{};
	std::string_view owner;
	char infoExist = 0;
	static const boost::regex re{ R"(([^;=]+)=(([^;=]+)(?:=[^;]*)?))" };
	for (boost::sregex_iterator it{ m[1].begin(), m[1].end(), re }, end; it != end; ++it) {
		auto factname = lc((*it)[1]);
		auto value = sv((*it)[2]);
		if (factname == "type"sv) {
			if (auto lcvalue = lc(value); lcvalue == "dir"sv)
				type = NODE_DIR;
			else if (lcvalue == "file"sv)
				type = NODE_FILE;
			// TODO: OS.unix=symlink、OS.unix=slinkの判定を行っているがバグっていて成功しない
		} else if (factname == "modify"sv) {
			infoExist |= FINFO_DATE | FINFO_TIME;
			SYSTEMTIME systemTime{};
			std::from_chars(value.data() + 0, value.data() + 4, systemTime.wYear);
			std::from_chars(value.data() + 4, value.data() + 6, systemTime.wMonth);
			std::from_chars(value.data() + 6, value.data() + 8, systemTime.wDay);
			std::from_chars(value.data() + 8, value.data() + 10, systemTime.wHour);
			std::from_chars(value.data() + 10, value.data() + 12, systemTime.wMinute);
			std::from_chars(value.data() + 12, value.data() + 14, systemTime.wSecond);
			SystemTimeToFileTime(&systemTime, &fileTime);
		} else if (factname == "size"sv) {
			infoExist |= FINFO_SIZE;
			size = parse<int64_t>(value);
		} else if (factname == "unix.mode"sv) {
			infoExist |= FINFO_ATTR;
			std::from_chars(value.data(), value.data() + value.size(), attr, 16);
		} else if (factname == "unix.owner"sv)
			owner = value;
	}
	return { { sv(m[2]), type, NO, size, attr, fileTime, owner, infoExist } };
}

static std::optional<FILELIST> ParseUnix(boost::smatch const& m) {
	SYSTEMTIME systemTime{};
	auto fixtimezone = false;
	char infoExist = FINFO_SIZE | FINFO_ATTR | FINFO_DATE;
	if (m[5].matched) {
		systemTime.wMonth = parsemonth(m[5]);
		systemTime.wDay = parse<WORD>(m[6]);
		if (m[7].matched) {
			systemTime.wYear = parse<WORD>(m[7]);
		} else {
			infoExist |= FINFO_TIME;
			SYSTEMTIME utcnow, localnow;
			GetSystemTime(&utcnow);
			TIME_ZONE_INFORMATION tz{ AskHostTimeZone() * -60 };
			SystemTimeToTzSpecificLocalTime(&tz, &utcnow, &localnow);
			systemTime.wHour = parse<WORD>(m[8]);
			systemTime.wMinute = parse<WORD>(m[9]);
			auto serialize = [](SYSTEMTIME const& st) { return (uint64_t)st.wMonth << 48 | (uint64_t)st.wDay << 32 | (uint64_t)st.wHour << 16 | st.wMinute; };
			systemTime.wYear = serialize(localnow) < serialize(systemTime) ? localnow.wYear - 1 : localnow.wYear;
			fixtimezone = true;
		}
	} else {
		systemTime.wYear = parse<WORD>(m[10]);
		systemTime.wMonth = parsemonth(m[11]);
		systemTime.wDay = parse<WORD>(m[12]);
	}
	auto ch = *m[1].begin();
	return { { sv(m[13]), ch == 'd' || ch == 'l' ? NODE_DIR : NODE_FILE, ch == 'l' ? YES : NO, parse<int64_t>(m[4]), parseattr(m[2]), tofiletime(systemTime, fixtimezone), sv(m[3]), infoExist } };
}

static std::optional<FILELIST> ParseLinux(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[5]), .wMonth = parse<WORD>(m[6]), .wDay = parse<WORD>(m[7]), .wHour = parse<WORD>(m[8]), .wMinute = parse<WORD>(m[9]) };
	auto ch = *m[1].begin();
	return { { sv(m[10]), ch == 'd' || ch == 'l' ? NODE_DIR : NODE_FILE, ch == 'l' ? YES : NO, parse<int64_t>(m[4]), parseattr(m[2]), tofiletime(systemTime, true), sv(m[3]), FINFO_SIZE | FINFO_ATTR | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseMelcom80(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parse<WORD>(m[7]), .wMonth = parsemonth(m[5]), .wDay = parse<WORD>(m[6]) };
	char node;
	auto ch = *m[1].begin();
	if (ch == 'd' || ch == 'l')
		node = NODE_DIR;
	else if (*m[9].begin() == 'B')
		node = NODE_FILE;
	else
		return {};
	return { { sv(m[8]), node, ch == 'l' ? YES : NO, parse<int64_t>(m[4]), parseattr(m[2]), tofiletime(systemTime), sv(m[3]), FINFO_SIZE | FINFO_ATTR | FINFO_DATE } };
}

static std::optional<FILELIST> ParseAgilent(boost::smatch const& m) {
	auto ch = *m[1].begin();
	return { { sv(m[5]), ch == 'd' || ch == 'l' ? NODE_DIR : NODE_FILE, ch == 'l' ? YES : NO, parse<int64_t>(m[4]), parseattr(m[2]), {}, sv(m[3]), FINFO_SIZE | FINFO_ATTR } };
}

static std::optional<FILELIST> ParseDos(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wHour = parse<WORD>(m[9]), .wMinute = parse<WORD>(m[10]) };
	if (m[1].matched) {
		systemTime.wYear = parseyear(m[1]);
		systemTime.wMonth = parse<WORD>(m[3]);
		systemTime.wDay = parse<WORD>(m[4]);
	} else {
		systemTime.wYear = parseyear(m[8]);
		systemTime.wMonth = parse<WORD>(m[5]);
		systemTime.wDay = parse<WORD>(m[7]);
	}
	if (m[11].matched)
		systemTime.wSecond = parse<WORD>(m[11]);
	if (auto ch = *m[12].begin(); ch == 'a' || ch == 'A') {
		if (systemTime.wHour == 12)
			systemTime.wHour = 0;
	} else
		if (systemTime.wHour < 12)
			systemTime.wHour += 12;
	if (*m[13].begin() == '<')
		return { { sv(m[14]), NODE_DIR, NO, 0, 0, tofiletime(systemTime, true), ""sv, FINFO_DATE | FINFO_TIME } };
	return { { sv(m[14]), NODE_FILE, NO, parse<int64_t>(m[13]), 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseChameleon(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[5]), .wMonth = parsemonth(m[3]), .wDay = parse<WORD>(m[4]), .wHour = parse<WORD>(m[6]), .wMinute = parse<WORD>(m[7]) };
	if (*m[2].begin() == '<')
		return { { sv(m[1]), NODE_DIR, 0, 0, 0, tofiletime(systemTime, true), ""sv, FINFO_DATE | FINFO_TIME } };
	return { { sv(m[1]), NODE_FILE, NO, parse<int64_t>(m[2]), 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseOs2(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[5]), .wMonth = parse<WORD>(m[3]), .wDay = parse<WORD>(m[4]), .wHour = parse<WORD>(m[6]), .wMinute = parse<WORD>(m[7]) };
	return { { sv(m[8]), m[2].matched ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(m[1]), 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseAllied(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[8]), .wMonth = parsemonth(m[3]), .wDay = parse<WORD>(m[4]), .wHour = parse<WORD>(m[5]), .wMinute = parse<WORD>(m[6]), .wSecond = parse<WORD>(m[7]) };
	if(*m[1].begin() == '<')
		return { { sv(m[2]), NODE_DIR, NO, 0, 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
	return { { sv(m[2]), NODE_FILE, NO, parse<int64_t>(m[1]), 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseShibasoku(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parse<WORD>(m[4]), .wMonth = parsemonth(m[2]), .wDay = parse<WORD>(m[3]), .wHour = parse<WORD>(m[5]), .wMinute = parse<WORD>(m[6]), .wSecond = parse<WORD>(m[7]) };
	return { { sv(m[8]), m[9].matched ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(m[1]), 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseAs400(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[3]), .wMonth = parse<WORD>(m[4]), .wDay = parse<WORD>(m[5]), .wHour = parse<WORD>(m[6]), .wMinute = parse<WORD>(m[7]), .wSecond = parse<WORD>(m[8]) };
	return { { sv(m[9]), m[10].matched ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(m[2]), 0, tofiletime(systemTime, true), sv(m[1]), FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseM1800(boost::smatch const& m) {
	FILETIME fileTime{};
	char infoExist = FINFO_ATTR;
	if (m[2].matched) {
		infoExist |= FINFO_DATE;
		SYSTEMTIME systemTime{ .wYear = parseyear(m[2]), .wMonth = parse<WORD>(m[3]), .wDay = parse<WORD>(m[4]) };
		fileTime = tofiletime(systemTime);
	}
	return { { sv(m[5]), m[6].matched ? NODE_DIR : NODE_FILE, NO, 0, parseattr(m[1]), fileTime, ""sv, infoExist } };
}

static std::optional<FILELIST> ParseGp6000(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[3]), .wMonth = parse<WORD>(m[4]), .wDay = parse<WORD>(m[5]), .wHour = parse<WORD>(m[6]), .wMinute = parse<WORD>(m[7]), .wSecond = parse<WORD>(m[8]) };
	auto ch = *m[1].begin();
	return { { sv(m[11]), ch == 'd' || ch == 'l' ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(m[10]), parseattr(m[2]), tofiletime(systemTime, true), sv(m[9]), FINFO_SIZE | FINFO_ATTR | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseOs7(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[4]), .wMonth = parse<WORD>(m[5]), .wDay = parse<WORD>(m[6]), .wHour = parse<WORD>(m[7]), .wMinute = parse<WORD>(m[8]), .wSecond = parse<WORD>(m[9]) };
	char infoExist = FINFO_ATTR | FINFO_DATE | FINFO_TIME;
	int64_t size = -1;
	if (m[3].matched) {
		infoExist |= FINFO_SIZE;
		size = parse<int64_t>(m[3]);
	}
	auto ch = *m[1].begin();
	return { { sv(m[10]), ch == 'd' || ch == 'l' ? NODE_DIR : NODE_FILE, NO, size, parseattr(m[2]), tofiletime(systemTime, true), ""sv, infoExist } };
}

static std::optional<FILELIST> ParseOs9(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[2]), .wMonth = parse<WORD>(m[3]), .wDay = parse<WORD>(m[4]), .wHour = parse<WORD>(m[5]), .wMinute = parse<WORD>(m[6]) };
	return { { sv(m[9]), *m[7].begin() == 'd' ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(m[8]), 0, tofiletime(systemTime, true), sv(m[1]), FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseIbm(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parse<WORD>(m[1]), .wMonth = parse<WORD>(m[2]), .wDay = parse<WORD>(m[3]) };
	return { { sv(m[5]), *m[4].begin() == 'O' ? NODE_DIR : NODE_FILE, NO, 0, 0, tofiletime(systemTime), ""sv, FINFO_DATE } };
}

static std::optional<FILELIST> ParseStratus(boost::smatch const& m) {
	static char type = NODE_NONE;
	if (m[1].matched)
		type = NODE_FILE;
	else if (m[2].matched)
		type = NODE_DIR;
	else if (m[3].matched)
		type = NODE_NONE;
	if (type == NODE_NONE)
		return {};
	int64_t size = 0;
	char infoExist = FINFO_DATE | FINFO_TIME;
	if (type == NODE_FILE) {
		size = parse<int64_t>(m[4]) * 4096;
		infoExist |= FINFO_SIZE;
	}
	SYSTEMTIME systemTime{ .wYear = parseyear(m[6]), .wMonth = parse<WORD>(m[7]), .wDay = parse<WORD>(m[8]), .wHour = parse<WORD>(m[9]), .wMinute = parse<WORD>(m[10]), .wSecond = parse<WORD>(m[11]) };
	return { { sv(m[12]), type, NO, size, 0, tofiletime(systemTime, true), sv(m[5]), infoExist } };
}

static std::optional<FILELIST> ParseVms(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parse<WORD>(m[7]), .wMonth = parsemonth(m[6]), .wDay = parse<WORD>(m[5]), .wHour = parse<WORD>(m[8]), .wMinute = parse<WORD>(m[9]), .wSecond = parse<WORD>(m[10]) };
#ifdef HAVE_OPENVMS
	constexpr int filename = 1;
#else
	constexpr int filename = 2;
#endif
	return { { sv(m[filename]), m[3].matched ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(m[4]) * 512, 0, tofiletime(systemTime, true), ""sv, FINFO_SIZE | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> ParseIrmx(boost::smatch const& m) {
	SYSTEMTIME systemTime{ .wYear = parseyear(m[7]), .wMonth = parsemonth(m[6]), .wDay = parse<WORD>(m[5]) };
	std::string size = m[3];
	size.erase(std::remove(begin(size), end(size), ','), end(size));
	return { { sv(m[1]), m[2].matched ? NODE_DIR : NODE_FILE, NO, parse<int64_t>(size), 0, tofiletime(systemTime), sv(m[4]), FINFO_SIZE | FINFO_DATE } };
}

static std::optional<FILELIST> ParseTandem(boost::smatch const& m) {
	// 属性にはFileCodeを充てる
	SYSTEMTIME systemTime{ .wYear = parseyear(m[6]), .wMonth = parsemonth(m[5]), .wDay = parse<WORD>(m[4]), .wHour = parse<WORD>(m[7]), .wMinute = parse<WORD>(m[8]), .wSecond = parse<WORD>(m[9]) };
	return { { sv(m[1]), NODE_FILE, NO, parse<int64_t>(m[3]), parse<int>(m[2]), tofiletime(systemTime, true), sv(m[10]), FINFO_SIZE | FINFO_ATTR | FINFO_DATE | FINFO_TIME } };
}

static std::optional<FILELIST> Parse(std::string const& line) {
	boost::smatch m;
	switch (AskHostType()) {
	case HTYPE_ACOS:
	case HTYPE_ACOS_4:
		return { { line.c_str(), NODE_FILE } };
	case HTYPE_VMS:
		return boost::regex_search(line, m, re::vms) ? ParseVms(m) : std::nullopt;
	case HTYPE_IRMX:
		return boost::regex_search(line, m, re::irmx) ? ParseIrmx(m) : std::nullopt;
	case HTYPE_STRATUS:
		return boost::regex_search(line, m, re::stratus) ? ParseStratus(m) : std::nullopt;
	case HTYPE_AGILENT:
		return boost::regex_search(line, m, re::agilent) ? ParseAgilent(m) : std::nullopt;
	case HTYPE_SHIBASOKU:
		return boost::regex_search(line, m, re::shibasoku) ? ParseShibasoku(m) : std::nullopt;
	}
#if defined(HAVE_TANDEM)
	if (AskRealHostType() == HTYPE_TANDEM)
		SetOSS(YES);
#endif
	if (boost::regex_search(line, m, re::mlsd))
		return ParseMlsd(m);
	if (boost::regex_search(line, m, re::unix))
		return ParseUnix(m);
	if (boost::regex_search(line, m, re::linux))
		return ParseLinux(m);
	if (boost::regex_search(line, m, re::dos))
		return ParseDos(m);
	if (boost::regex_search(line, m, re::melcom80))
		return ParseMelcom80(m);
	if (boost::regex_search(line, m, re::agilent))
		return ParseAgilent(m);
	if (boost::regex_search(line, m, re::as400))
		return ParseAs400(m);
	if (boost::regex_search(line, m, re::m1800))
		return ParseM1800(m);
	if (boost::regex_search(line, m, re::gp6000))
		return ParseGp6000(m);
	if (boost::regex_search(line, m, re::chameleon))
		return ParseChameleon(m);
	if (boost::regex_search(line, m, re::os2))
		return ParseOs2(m);
	if (boost::regex_search(line, m, re::os7))
		return ParseOs7(m);
	if (boost::regex_search(line, m, re::os9))
		return ParseOs9(m);
	if (boost::regex_search(line, m, re::allied))
		return ParseAllied(m);
	if (boost::regex_search(line, m, re::ibm))
		return ParseIbm(m);
	if (boost::regex_search(line, m, re::shibasoku))
		return ParseShibasoku(m);
	if (boost::regex_search(line, m, re::stratus))
		return ParseStratus(m);
	if (boost::regex_search(line, m, re::vms))
		return ParseVms(m);
	if (boost::regex_search(line, m, re::irmx))
		return ParseIrmx(m);
	if (boost::regex_search(line, m, re::tandem)) {
#if defined(HAVE_TANDEM)
		SetOSS(NO);
#endif
		return ParseTandem(m);
	}
	return {};
}

// ファイル一覧情報の１行を取得
static std::optional<std::vector<std::variant<FILELIST, std::string>>> GetListLine(int Num) {
	std::ifstream is{ MakeCacheFileName(Num), std::ios::binary };
	if (!is)
		return {};
	std::vector<std::variant<FILELIST, std::string>> lines;
	for (std::string line; getline(is, line);) {
		if (DebugConsole == YES) {
			std::wstring wline;
			for (auto ch : line)
				if ('\x20' <= ch && ch != '%' && ch <= '\x7E')
					wline += (wchar_t)ch;
				else
					wline += std::format(L"%{:02X}"sv, (unsigned)ch);
			Debug(L"{}"sv, wline);
		}
		line.erase(std::remove(begin(line), end(line), '\r'), end(line));
		std::replace(begin(line), end(line), '\b', ' ');
		if (auto result = Parse(line))
			lines.emplace_back(std::move(result).value());
		else
			lines.emplace_back(std::move(line));
	}
	if (CurHost.NameKanjiCode == KANJI_AUTO) {
		CodeDetector cd;
		for (auto const& line : lines)
			if (auto p = std::get_if<FILELIST>(&line); p && p->Node != NODE_NONE && !empty(p->Original))
				cd.Test(p->Original);
		CurHost.CurNameKanjiCode = cd.result();
	} else
		CurHost.CurNameKanjiCode = CurHost.NameKanjiCode;
	for (auto& line : lines) {
		if (auto p = std::get_if<FILELIST>(&line); p && p->Node != NODE_NONE && !empty(p->Original)) {
			auto file = ConvertFrom(p->Original, CurHost.CurNameKanjiCode);
			if (auto last = file.back(); last == '/' || last == '\\')
				file.resize(file.size() - 1);
			if (empty(file) || file == L"."sv || file == L".."sv)
				p->Node = NODE_NONE;
			p->Name = file;
		}
	}
	return lines;
}


// ローカル側のサブディレクトリ以下のファイルをリストに登録する
static bool MakeLocalTree(fs::path const& path, std::vector<FILELIST>& Base) {
	std::vector<WIN32_FIND_DATAW> items;
	if (!FindFile(path / L"*", [&items](auto const& item) { items.push_back(item); return true; }))
		return false;
	for (auto const& data : items)
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && AskFilterStr(data.cFileName, NODE_FILE) == YES) {
			FILELIST Pkt{};
			Pkt.Name = (path / data.cFileName).generic_wstring();
			Pkt.Node = NODE_FILE;
			Pkt.Size = LONGLONG(data.nFileSizeHigh) << 32 | data.nFileSizeLow;
			if (SYSTEMTIME TmpStime; FileTimeToSystemTime(&data.ftLastWriteTime, &TmpStime)) {
				if (DispTimeSeconds == NO)
					TmpStime.wSecond = 0;
				TmpStime.wMilliseconds = 0;
				SystemTimeToFileTime(&TmpStime, &Pkt.Time);
			}
			AddFileList(Pkt, Base);
		}
	for (auto const& data : items)
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			FILELIST Pkt{};
			auto const src = path / data.cFileName;
			Pkt.Name = src.generic_wstring();
			Pkt.Node = NODE_DIR;
			AddFileList(Pkt, Base);
			if (!MakeLocalTree(src, Base))
				return false;
		}
	return true;
}


/*----- ファイルリストに情報を登録する ----------------------------------------
*
*	Parameter
*		FILELIST *Pkt : 登録するファイル情報
*		FILELIST **Base : ファイルリストの先頭
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void AddFileList(FILELIST const& Pkt, std::vector<FILELIST>& Base) {
	Debug(L"FileList: NODE={}: {}."sv, Pkt.Node, Pkt.Name);
	/* リストの重複を取り除く */
	if (std::ranges::any_of(Base, [&Pkt](auto const& f) { return Pkt.Name == f.Name; })) {
		Debug(L" --> Duplicate!!"sv);
		return;
	}
	Base.emplace_back(Pkt);
}


/*----- ファイルリストに指定のファイルがあるかチェック ------------------------
*
*	Parameter
*		char *Fname : ファイル名
*		FILELIST *Base : ファイルリストの先頭
*		int Caps : 大文字/小文字の区別モード (COMP_xxx)
*
*	Return Value
*		FILELIST *見つかったファイルリストのデータ
*			NULL=見つからない
*----------------------------------------------------------------------------*/

const FILELIST* SearchFileList(std::wstring_view Fname, std::vector<FILELIST> const& Base, int Caps) {
	for (auto p = data(Base), end = data(Base) + size(Base); p != end; ++p)
		if (Caps == COMP_STRICT ? Fname == p->Name : ieq(Fname, p->Name) && (Caps == COMP_IGNORE || lc(p->Name) == p->Name))
			return p;
	return nullptr;
}


// フィルタに指定されたファイル名かどうかを返す
static int AskFilterStr(std::wstring const& file, int Type) {
	static boost::wregex re{ L";" };
	if (Type != NODE_FILE || empty(FilterStr) || FilterStr == L"*"sv)
		return YES;
	for (boost::wsregex_token_iterator it{ begin(FilterStr), end(FilterStr), re, -1 }, end; it != end; ++it)
		if (it->matched && CheckFname(file, *it))
			return YES;
	return NO;
}


// フィルタを設定する
void SetFilter(int *CancelCheckWork) {
	struct Filter {
		using result_t = bool;
		INT_PTR OnInit(HWND hDlg) {
			SendDlgItemMessageW(hDlg, FILTER_STR, EM_LIMITTEXT, FILTER_EXT_LEN + 1, 0);
			SetText(hDlg, FILTER_STR, FilterStr);
			return TRUE;
		}
		void OnCommand(HWND hDlg, WORD id) {
			switch (id) {
			case IDOK:
				FilterStr = GetText(hDlg, FILTER_STR);
				EndDialog(hDlg, true);
				break;
			case IDCANCEL:
				EndDialog(hDlg, false);
				break;
			case FILTER_NOR:
				FilterStr = L"*"s;
				EndDialog(hDlg, true);
				break;
			case IDHELP:
				ShowHelp(IDH_HELP_TOPIC_0000021);
				break;
			}
		}
	};
	if (Dialog(GetFtpInst(), filter_dlg, GetMainHwnd(), Filter{})) {
		DispWindowTitle();
		UpdateStatusBar();
		GetLocalDirForWnd();
		GetRemoteDirForWnd(CACHE_LASTREAD, CancelCheckWork);
	}
}
