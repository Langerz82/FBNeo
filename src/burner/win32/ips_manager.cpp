#include "burner.h"

#define NUM_LANGUAGES		12
#define MAX_NODES			1024
#define MAX_ACTIVE_PATCHES	1024

#define IPS_OFFSET_016		0x1000000
#define IPS_OFFSET_032		0x2000000
#define IPS_OFFSET_048		0x3000000
#define IPS_OFFSET_064		0x4000000
#define IPS_OFFSET_080		0x5000000
#define IPS_OFFSET_096		0x6000000
#define IPS_OFFSET_112		0x7000000
#define IPS_OFFSET_128		0x8000000
#define IPS_OFFSET_144		0x9000000

static HWND hIpsDlg			= NULL;
static HWND hParent			= NULL;
static HWND hIpsList		= NULL;

int nIpsSelectedLanguage	= 0;
static TCHAR szFullName[1024];
static TCHAR szLanguages[NUM_LANGUAGES][32];
static TCHAR szLanguageCodes[NUM_LANGUAGES][6];
static TCHAR szPngName[MAX_PATH];

static HTREEITEM hItemHandles[MAX_NODES];

static int nPatchIndex		= 0;
static int nNumPatches		= 0;
static HTREEITEM hPatchHandlesIndex[MAX_NODES];
static TCHAR szPatchFileNames[MAX_NODES][MAX_PATH];

static HBRUSH hWhiteBGBrush;
static HBITMAP hBmp			= NULL;
static HBITMAP hPreview		= NULL;

static TCHAR szDriverName[32];

static int nRomOffset		= 0;

INT32 nIpsMaxFileLen		= 0;

TCHAR szIpsActivePatches[MAX_ACTIVE_PATCHES][MAX_PATH];

// GCC doesn't seem to define these correctly.....
#define _TreeView_SetItemState(hwndTV, hti, data, _mask) \
{ TVITEM _ms_TVi;\
  _ms_TVi.mask = TVIF_STATE; \
  _ms_TVi.hItem = hti; \
  _ms_TVi.stateMask = _mask;\
  _ms_TVi.state = data;\
  SNDMSG((hwndTV), TVM_SETITEM, 0, (LPARAM)(TV_ITEM *)&_ms_TVi);\
}

#define _TreeView_SetCheckState(hwndTV, hti, fCheck) \
  _TreeView_SetItemState(hwndTV, hti, INDEXTOSTATEIMAGEMASK((fCheck)?2:1), TVIS_STATEIMAGEMASK)

#define _TreeView_GetCheckState(hwndTV, hti) \
   ((((UINT)(SNDMSG((hwndTV), TVM_GETITEMSTATE, (WPARAM)(hti), TVIS_STATEIMAGEMASK))) >> 12) -1)

static TCHAR* GameIpsConfigName()
{
	// Return the path of the config file for this game
	static TCHAR szName[64];
	_stprintf(szName, _T("config\\ips\\%s.ini"), szDriverName);
	return szName;
}

int GetIpsNumPatches()
{
	WIN32_FIND_DATA wfd;
	HANDLE hSearch;
	TCHAR szFilePath[MAX_PATH];
	int Count = 0;

	_stprintf(szFilePath, _T("%s%s\\"), szAppIpsPath, BurnDrvGetText(DRV_NAME));
	_tcscat(szFilePath, _T("*.dat"));

	hSearch = FindFirstFile(szFilePath, &wfd);

	if (hSearch != INVALID_HANDLE_VALUE) {
		int Done = 0;

		while (!Done ) {
			Count++;
			Done = !FindNextFile(hSearch, &wfd);
		}

		FindClose(hSearch);
	}

	return Count;
}

static TCHAR* GetPatchDescByLangcode(FILE* fp, int nLang)
{
	TCHAR* result = NULL;
	char* desc = NULL;
	char langtag[10];

	sprintf(langtag, "[%s]", TCHARToANSI(szLanguageCodes[nLang], NULL, 0));

	fseek(fp, 0, SEEK_SET);

	while (!feof(fp))
	{
		char s[4096];

		if (fgets(s, sizeof(s), fp) != NULL)
		{
			if (strncmp(langtag, s, strlen(langtag)) != 0)
				continue;

			while (fgets(s, sizeof(s), fp) != NULL)
			{
				char* p;

				if (*s == '[')
				{
					if (desc)
					{
						result = tstring_from_utf8(desc);
						if (desc) {
							free(desc);
							desc = NULL;
						}
						return result;
					}
					else
						return NULL;
				}

				for (p = s; *p; p++)
				{
					if (*p == '\r' || *p == '\n')
					{
						*p = '\0';
						break;
					}
				}

				if (desc)
				{
					char* p1;
					int len = strlen(desc);

					len += strlen(s) + 2;
					p1 = (char*)malloc(len + 1);
					sprintf(p1, "%s\r\n%s", desc, s);
					if (desc) {
						free(desc);
					}
					desc = p1;
				}
				else
				{
					desc = (char*)malloc(strlen(s) + 1);
					if (desc != NULL)
						strcpy(desc, s);
				}
			}
		}
	}

	if (desc)
	{
		result = tstring_from_utf8(desc);
		if (desc) {
			free(desc);
			desc = NULL;
		}
		return result;
	}
	else
		return NULL;
}

static void FillListBox()
{
	WIN32_FIND_DATA wfd;
	HANDLE hSearch;
	TCHAR szFilePath[MAX_PATH];
	TCHAR szFilePathSearch[MAX_PATH];
	TCHAR szFileName[MAX_PATH];
	TCHAR *PatchDesc = NULL;
	TCHAR PatchName[256];
	int nHandlePos = 0;

	TV_INSERTSTRUCT TvItem;

	memset(&TvItem, 0, sizeof(TvItem));
	TvItem.item.mask = TVIF_TEXT | TVIF_PARAM;
	TvItem.hInsertAfter = TVI_LAST;

	_stprintf(szFilePath, _T("%s%s\\"), szAppIpsPath, szDriverName);
	_stprintf(szFilePathSearch, _T("%s*.dat"), szFilePath);

	hSearch = FindFirstFile(szFilePathSearch, &wfd);

	if (hSearch != INVALID_HANDLE_VALUE) {
		int Done = 0;

		while (!Done ) {
			memset(szFileName, '\0', MAX_PATH * sizeof(TCHAR));
			_stprintf(szFileName, _T("%s%s"), szFilePath, wfd.cFileName);

			FILE *fp = _tfopen(szFileName, _T("r"));
            if (fp) {
                bool AllocDesc = false;
				PatchDesc = NULL;
				memset(PatchName, '\0', 256 * sizeof(TCHAR));

				PatchDesc = GetPatchDescByLangcode(fp, nIpsSelectedLanguage);
				// If not available - try English first
				if (PatchDesc == NULL) PatchDesc = GetPatchDescByLangcode(fp, 0);
				// Simplified Chinese is the reference language (should always be available!!)
				if (PatchDesc == NULL) PatchDesc = GetPatchDescByLangcode(fp, 1);

				bprintf(0, _T("PatchDesc [%s]\n"), PatchDesc);

                if (PatchDesc == NULL) {
                    PatchDesc = (TCHAR*)malloc(1024);
                    memset(PatchDesc, 0, 1024);
                    AllocDesc = true;
                    _stprintf(PatchDesc, _T("%s"), wfd.cFileName);
                }

				for (unsigned int i = 0; i < _tcslen(PatchDesc); i++) {
					if (PatchDesc[i] == '\r' || PatchDesc[i] == '\n') break;
					PatchName[i] = PatchDesc[i];
				}

                if (AllocDesc) {
                    free(PatchDesc);
                }

				// Check for categories
				TCHAR *Tokens;
				int nNumTokens = 0;
				int nNumNodes = 0;
				TCHAR szCategory[256];
				unsigned int nPatchNameLength = _tcslen(PatchName);

				Tokens = _tcstok(PatchName, _T("/"));
				while (Tokens != NULL) {
					if (nNumTokens == 0) {
						int bAddItem = 1;
						// Check if item already exists
						nNumNodes = SendMessage(hIpsList, TVM_GETCOUNT, (WPARAM)0, (LPARAM)0);
						for (int i = 0; i < nNumNodes; i++) {
							TCHAR Temp[256];
							TVITEM Tvi;
							memset(&Tvi, 0, sizeof(Tvi));
							Tvi.hItem = hItemHandles[i];
							Tvi.mask = TVIF_TEXT | TVIF_HANDLE;
							Tvi.pszText = Temp;
							Tvi.cchTextMax = 256;
							SendMessage(hIpsList, TVM_GETITEM, (WPARAM)0, (LPARAM)&Tvi);

							if (!_tcsicmp(Tvi.pszText, Tokens)) bAddItem = 0;
						}

						if (bAddItem) {
							TvItem.hParent = TVI_ROOT;
							TvItem.item.pszText = Tokens;
							hItemHandles[nHandlePos] = (HTREEITEM)SendMessage(hIpsList, TVM_INSERTITEM, 0, (LPARAM)&TvItem);
							nHandlePos++;
						}

						if (_tcslen(Tokens) == nPatchNameLength) {
							hPatchHandlesIndex[nPatchIndex] = hItemHandles[nHandlePos - 1];
							_tcscpy(szPatchFileNames[nPatchIndex], szFileName);

							nPatchIndex++;
						}

						_tcscpy(szCategory, Tokens);
					} else {
						HTREEITEM hNode = TVI_ROOT;
						// See which category we should be in
						nNumNodes = SendMessage(hIpsList, TVM_GETCOUNT, (WPARAM)0, (LPARAM)0);
						for (int i = 0; i < nNumNodes; i++) {
							TCHAR Temp[256];
							TVITEM Tvi;
							memset(&Tvi, 0, sizeof(Tvi));
							Tvi.hItem = hItemHandles[i];
							Tvi.mask = TVIF_TEXT | TVIF_HANDLE;
							Tvi.pszText = Temp;
							Tvi.cchTextMax = 256;
							SendMessage(hIpsList, TVM_GETITEM, (WPARAM)0, (LPARAM)&Tvi);

							if (!_tcsicmp(Tvi.pszText, szCategory)) hNode = Tvi.hItem;
						}

						TvItem.hParent = hNode;
						TvItem.item.pszText = Tokens;
						hItemHandles[nHandlePos] = (HTREEITEM)SendMessage(hIpsList, TVM_INSERTITEM, 0, (LPARAM)&TvItem);

						hPatchHandlesIndex[nPatchIndex] = hItemHandles[nHandlePos];
						_tcscpy(szPatchFileNames[nPatchIndex], szFileName);

						nHandlePos++;
						nPatchIndex++;
					}

					Tokens = _tcstok(NULL, _T("/"));
					nNumTokens++;
				}

				fclose(fp);
			}

			Done = !FindNextFile(hSearch, &wfd);
		}

		FindClose(hSearch);
	}

	nNumPatches = nPatchIndex;

	// Expand all branches
	int nNumNodes = SendMessage(hIpsList, TVM_GETCOUNT, (WPARAM)0, (LPARAM)0);;
	for (int i = 0; i < nNumNodes; i++) {
		SendMessage(hIpsList, TVM_EXPAND, TVE_EXPAND, (LPARAM)hItemHandles[i]);
	}
}

int GetIpsNumActivePatches()
{
	int nActivePatches = 0;

	for (int i = 0; i < MAX_ACTIVE_PATCHES; i++) {
		if (_tcsicmp(szIpsActivePatches[i], _T(""))) nActivePatches++;
	}

	return nActivePatches;
}

void LoadIpsActivePatches()
{
    _tcscpy(szDriverName, BurnDrvGetText(DRV_NAME));

	for (int i = 0; i < MAX_ACTIVE_PATCHES; i++) {
		_stprintf(szIpsActivePatches[i], _T(""));
	}

	FILE* fp = _tfopen(GameIpsConfigName(), _T("rt"));
	TCHAR szLine[MAX_PATH];
	int nActivePatches = 0;

    if (fp) {
		while (_fgetts(szLine, sizeof(szLine), fp)) {
			int nLen = _tcslen(szLine);

			// Get rid of the linefeed at the end
			if (nLen > 0 && szLine[nLen - 1] == 10) {
				szLine[nLen - 1] = 0;
				nLen--;
			}

			if (!_tcsnicmp(szLine, _T("//"), 2)) continue;
			if (!_tcsicmp(szLine, _T(""))) continue;

			_stprintf(szIpsActivePatches[nActivePatches], _T("%s%s\\%s"), szAppIpsPath, szDriverName, szLine);
			nActivePatches++;
		}

		fclose(fp);
    }
}

static void CheckActivePatches()
{
	LoadIpsActivePatches();

	int nActivePatches = GetIpsNumActivePatches();

	for (int i = 0; i < nActivePatches; i++) {
		for (int j = 0; j < nNumPatches; j++) {
			if (!_tcsicmp(szIpsActivePatches[i], szPatchFileNames[j])) {
				_TreeView_SetCheckState(hIpsList, hPatchHandlesIndex[j], TRUE);
			}
		}
	}
}

static int IpsManagerInit()
{
	// Get the games full name
	TCHAR szText[1024] = _T("");
	TCHAR* pszPosition = szText;
	TCHAR* pszName = BurnDrvGetText(DRV_FULLNAME);

	pszPosition += _sntprintf(szText, 1024, pszName);

	pszName = BurnDrvGetText(DRV_FULLNAME);
	while ((pszName = BurnDrvGetText(DRV_NEXTNAME | DRV_FULLNAME)) != NULL) {
		if (pszPosition + _tcslen(pszName) - 1024 > szText) {
			break;
		}
		pszPosition += _stprintf(pszPosition, _T(SEPERATOR_2) _T("%s"), pszName);
	}

	_tcscpy(szFullName, szText);

	_stprintf(szText, _T("%s") _T(SEPERATOR_1) _T("%s"), FBALoadStringEx(hAppInst, IDS_IPSMANAGER_TITLE, true), szFullName);

	// Set the window caption
	SetWindowText(hIpsDlg, szText);

	// Fill the combo box
	_stprintf(szLanguages[0], FBALoadStringEx(hAppInst, IDS_LANG_ENGLISH_US, true));
	_stprintf(szLanguages[1], FBALoadStringEx(hAppInst, IDS_LANG_SIMP_CHINESE, true));
	_stprintf(szLanguages[2], FBALoadStringEx(hAppInst, IDS_LANG_TRAD_CHINESE, true));
	_stprintf(szLanguages[3], FBALoadStringEx(hAppInst, IDS_LANG_JAPANESE, true));
	_stprintf(szLanguages[4], FBALoadStringEx(hAppInst, IDS_LANG_KOREAN, true));
	_stprintf(szLanguages[5], FBALoadStringEx(hAppInst, IDS_LANG_FRENCH, true));
	_stprintf(szLanguages[6], FBALoadStringEx(hAppInst, IDS_LANG_SPANISH, true));
	_stprintf(szLanguages[7], FBALoadStringEx(hAppInst, IDS_LANG_ITALIAN, true));
	_stprintf(szLanguages[8], FBALoadStringEx(hAppInst, IDS_LANG_GERMAN, true));
	_stprintf(szLanguages[9], FBALoadStringEx(hAppInst, IDS_LANG_PORTUGUESE, true));
	_stprintf(szLanguages[10], FBALoadStringEx(hAppInst, IDS_LANG_POLISH, true));
	_stprintf(szLanguages[11], FBALoadStringEx(hAppInst, IDS_LANG_HUNGARIAN, true));

	_stprintf(szLanguageCodes[0], _T("en_US"));
	_stprintf(szLanguageCodes[1], _T("zh_CN"));
	_stprintf(szLanguageCodes[2], _T("zh_TW"));
	_stprintf(szLanguageCodes[3], _T("ja_JP"));
	_stprintf(szLanguageCodes[4], _T("ko_KR"));
	_stprintf(szLanguageCodes[5], _T("fr_FR"));
	_stprintf(szLanguageCodes[6], _T("es_ES"));
	_stprintf(szLanguageCodes[7], _T("it_IT"));
	_stprintf(szLanguageCodes[8], _T("de_DE"));
	_stprintf(szLanguageCodes[9], _T("pt_BR"));
	_stprintf(szLanguageCodes[10], _T("pl_PL"));
	_stprintf(szLanguageCodes[11], _T("hu_HU"));

	for (int i = 0; i < NUM_LANGUAGES; i++) {
		SendDlgItemMessage(hIpsDlg, IDC_CHOOSE_LIST, CB_ADDSTRING, 0, (LPARAM)&szLanguages[i]);
	}

	SendDlgItemMessage(hIpsDlg, IDC_CHOOSE_LIST, CB_SETCURSEL, (WPARAM)nIpsSelectedLanguage, (LPARAM)0);

	hIpsList = GetDlgItem(hIpsDlg, IDC_TREE1);

	_tcscpy(szDriverName, BurnDrvGetText(DRV_NAME));

	FillListBox();

	CheckActivePatches();

	return 0;
}

static void RefreshPatch()
{
	szPngName[0] = _T('\0');  // Reset the file name of the preview picture
	SendMessage(GetDlgItem(hIpsDlg, IDC_TEXTCOMMENT), WM_SETTEXT, (WPARAM)0, (LPARAM)NULL);
	SendDlgItemMessage(hIpsDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hPreview);

	HTREEITEM hSelectHandle = (HTREEITEM)SendMessage(hIpsList, TVM_GETNEXTITEM, TVGN_CARET, ~0U);

	if (hBmp) {
		DeleteObject((HGDIOBJ)hBmp);
		hBmp = NULL;
	}

	for (int i = 0; i < nNumPatches; i++) {
		if (hSelectHandle == hPatchHandlesIndex[i]) {
			TCHAR *PatchDesc = NULL;

			FILE *fp = _tfopen(szPatchFileNames[i], _T("r"));
			if (fp) {
				PatchDesc = GetPatchDescByLangcode(fp, nIpsSelectedLanguage);
				// If not available - try English first
				if (PatchDesc == NULL) PatchDesc = GetPatchDescByLangcode(fp, 0);
				// Simplified Chinese is the reference language (should always be available!!)
				if (PatchDesc == NULL) PatchDesc = GetPatchDescByLangcode(fp, 1);

				SendMessage(GetDlgItem(hIpsDlg, IDC_TEXTCOMMENT), WM_SETTEXT, (WPARAM)0, (LPARAM)PatchDesc);

				fclose(fp);
			}
			fp = NULL;

			TCHAR szImageFileName[MAX_PATH];
			szImageFileName[0] = _T('\0');

			_tcscpy(szImageFileName, szPatchFileNames[i]);
			szImageFileName[_tcslen(szImageFileName) - 3] = _T('p');
			szImageFileName[_tcslen(szImageFileName) - 2] = _T('n');
			szImageFileName[_tcslen(szImageFileName) - 1] = _T('g');

			fp = _tfopen(szImageFileName, _T("rb"));
			HBITMAP hNewImage = NULL;
			if (fp) {
				_tcscpy(szPngName, szImageFileName);  // Associated preview picture
				hNewImage = PNGLoadBitmap(hIpsDlg, fp, 304, 228, 3);
				fclose(fp);
			}

			if (hNewImage) {
				DeleteObject((HGDIOBJ)hBmp);
				hBmp = hNewImage;
				SendDlgItemMessage(hIpsDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
			} else {
				SendDlgItemMessage(hIpsDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hPreview);
			}
		}
	}
}

static void SavePatches()
{
	int nActivePatches = 0;

	for (int i = 0; i < MAX_ACTIVE_PATCHES; i++) {
		_stprintf(szIpsActivePatches[i], _T(""));
	}

	for (int i = 0; i < nNumPatches; i++) {
		int nChecked = _TreeView_GetCheckState(hIpsList, hPatchHandlesIndex[i]);

		if (nChecked) {
			_tcscpy(szIpsActivePatches[nActivePatches], szPatchFileNames[i]);
			nActivePatches++;
		}
	}

	FILE* fp = _tfopen(GameIpsConfigName(), _T("wt"));

	if (fp) {
		_ftprintf(fp, _T("// ") _T(APP_TITLE) _T(" v%s --- IPS Config File for %s (%s)\n\n"), szAppBurnVer, szDriverName, szFullName);
		for (int i = 0; i < nActivePatches; i++) {
			TCHAR *Tokens;
			TCHAR szFileName[MAX_PATH];
			Tokens = _tcstok(szIpsActivePatches[i], _T("\\"));
			while (Tokens != NULL) {
				szFileName[0] = _T('\0');
				_tcscpy(szFileName, Tokens);
				Tokens = _tcstok(NULL, _T("\\"));
			}

			_ftprintf(fp, _T("%s\n"), szFileName);
		}
		fclose(fp);
	}
}

static void IpsManagerExit()
{
	SendDlgItemMessage(hIpsDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);

	for (int i = 0; i < NUM_LANGUAGES; i++) {
		szLanguages[i][0] = _T('\0');
		szLanguageCodes[i][0] = _T('\0');
	}

	memset(hItemHandles, 0, MAX_NODES * sizeof(HTREEITEM));
	memset(hPatchHandlesIndex, 0, MAX_NODES * sizeof(HTREEITEM));

	nPatchIndex = 0;
	nNumPatches = 0;

	for (int i = 0; i < MAX_NODES; i++) {
		szPatchFileNames[i][0] = _T('\0');
	}

	if (hBmp) {
		DeleteObject((HGDIOBJ)hBmp);
		hBmp = NULL;
	}

	if (hPreview) {
		DeleteObject((HGDIOBJ)hPreview);
		hPreview = NULL;
	}

	DeleteObject(hWhiteBGBrush);

	hParent = NULL;

	EndDialog(hIpsDlg, 0);
}

static void IpsOkay()
{
	SavePatches();
	IpsManagerExit();
}

static INT_PTR CALLBACK DefInpProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg) {
		case WM_INITDIALOG: {
			hIpsDlg = hDlg;

			hWhiteBGBrush = CreateSolidBrush(RGB(0xFF,0xFF,0xFF));
			hPreview = PNGLoadBitmap(hIpsDlg, NULL, 304, 228, 2);
			SendDlgItemMessage(hIpsDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hPreview);

			LONG_PTR Style;
			Style = GetWindowLongPtr (GetDlgItem(hIpsDlg, IDC_TREE1), GWL_STYLE);
			Style |= TVS_CHECKBOXES;
			SetWindowLongPtr (GetDlgItem(hIpsDlg, IDC_TREE1), GWL_STYLE, Style);

			IpsManagerInit();
			int nBurnDrvActiveOld = nBurnDrvActive;		// RockyWall Add
			WndInMid(hDlg, hScrnWnd);
			SetFocus(hDlg);								// Enable Esc=close
			nBurnDrvActive = nBurnDrvActiveOld;			// RockyWall Add
			break;
		}

		case WM_LBUTTONDBLCLK: {
			RECT PreviewRect;
			POINT Point;

			memset(&PreviewRect, 0, sizeof(RECT));
			memset(&Point, 0, sizeof(POINT));

			if (GetCursorPos(&Point) && GetWindowRect(GetDlgItem(hIpsDlg, IDC_SCREENSHOT_H), &PreviewRect)) {
				if (PtInRect(&PreviewRect, Point)) {
					FILE* fp = NULL;

					fp = _tfopen(szPngName, _T("rb"));
					if (fp) {
						fclose(fp);
						ShellExecute(  // Open the image with the associated program
							GetDlgItem(hIpsDlg, IDC_SCREENSHOT_H),
							NULL,
							szPngName,
							NULL,
							NULL,
							SW_SHOWNORMAL);
					}
				}
			}
			return 0;
		}

		case WM_COMMAND: {
			int wID = LOWORD(wParam);
			int Notify = HIWORD(wParam);

			if (Notify == BN_CLICKED) {
				switch (wID) {
					case IDOK: {
						IpsOkay();
						break;
					}

					case IDCANCEL: {
						SendMessage(hDlg, WM_CLOSE, 0, 0);
						return 0;
					}

					case IDC_IPSMAN_DESELECTALL: {
						for (int i = 0; i < nNumPatches; i++) {
							for (int j = 0; j < nNumPatches; j++) {
								_TreeView_SetCheckState(hIpsList, hPatchHandlesIndex[j], FALSE);
							}
						}
						break;
					}
				}
			}

			if (wID == IDC_CHOOSE_LIST && Notify == CBN_SELCHANGE) {
				nIpsSelectedLanguage = SendMessage(GetDlgItem(hIpsDlg, IDC_CHOOSE_LIST), CB_GETCURSEL, 0, 0);
				TreeView_DeleteAllItems(hIpsList);
				FillListBox();
				RefreshPatch();
				return 0;
			}
			break;
		}

		case WM_NOTIFY: {
			NMHDR* pNmHdr = (NMHDR*)lParam;

			if (LOWORD(wParam) == IDC_TREE1 && pNmHdr->code == TVN_SELCHANGED) {
				RefreshPatch();

				return 1;
			}

			if (LOWORD(wParam) == IDC_TREE1 && pNmHdr->code == NM_DBLCLK) {
				// disable double-click node-expand
				SetWindowLongPtr(hIpsDlg, DWLP_MSGRESULT, 1);

				return 1;
			}

			if (LOWORD(wParam) == IDC_TREE1 && pNmHdr->code == NM_CLICK) {
				POINT cursorPos;
				GetCursorPos(&cursorPos);
				ScreenToClient(hIpsList, &cursorPos);

				TVHITTESTINFO thi;
				thi.pt = cursorPos;
				TreeView_HitTest(hIpsList, &thi);

				if (thi.flags == TVHT_ONITEMSTATEICON) {
					TreeView_SelectItem(hIpsList, thi.hItem);
				}

				return 1;
			}

			if (LOWORD(wParam) == IDC_CHOOSE_LIST && pNmHdr->code == NM_DBLCLK) {
				// disable double-click node-expand
				SetWindowLongPtr(hIpsDlg, DWLP_MSGRESULT, 1);

				return 1;
			}

			SetWindowLongPtr(hIpsDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
			return 1;
		}

		case WM_CTLCOLORSTATIC: {
			if ((HWND)lParam == GetDlgItem(hIpsDlg, IDC_TEXTCOMMENT)) {
				return (INT_PTR)hWhiteBGBrush;
			}
			break;
		}

		case WM_CLOSE: {
			IpsManagerExit();
			break;
		}
	}

	return 0;
}

int IpsManagerCreate(HWND hParentWND)
{
	hParent = hParentWND;

	FBADialogBox(hAppInst, MAKEINTRESOURCE(IDD_IPS_MANAGER), hParent, (DLGPROC)DefInpProc);
	return 1;
}

// Game patching

#define UTF8_SIGNATURE	"\xef\xbb\xbf"
#define IPS_SIGNATURE	"PATCH"
#define IPS_TAG_EOF	"EOF"
#define IPS_EXT		".ips"

#define BYTE3_TO_UINT(bp) \
     (((unsigned int)(bp)[0] << 16) & 0x00FF0000) | \
     (((unsigned int)(bp)[1] << 8) & 0x0000FF00) | \
     ((unsigned int)(bp)[2] & 0x000000FF)

#define BYTE2_TO_UINT(bp) \
    (((unsigned int)(bp)[0] << 8) & 0xFF00) | \
    ((unsigned int) (bp)[1] & 0x00FF)

bool bDoIpsPatch = false;

static void PatchFile(const char* ips_path, UINT8* base, bool readonly)
{
	char buf[6];
	FILE* f = NULL;
	int Offset, Size;
	UINT8* mem8 = NULL;

	if (NULL == (f = fopen(ips_path, "rb"))) {
		bprintf(0, _T("IPS - Can't open file %S!  Aborting.\n"), ips_path);
		return;
	}

	memset(buf, 0, sizeof(buf));
	fread(buf, 1, 5, f);
	if (strcmp(buf, IPS_SIGNATURE)) {
		bprintf(0, _T("IPS - Bad IPS-Signature in: %S.\n"), ips_path);
		if (f)
		{
			fclose(f);
		}
		return;
	} else {
		bprintf(0, _T("IPS - Patching with: %S.\n"), ips_path);
		UINT8 ch = 0;
		int bRLE = 0;
		while (!feof(f)) {
			// read patch address offset
			fread(buf, 1, 3, f);
			buf[3] = 0;
			if (strcmp(buf, IPS_TAG_EOF) == 0)
				break;

			Offset = BYTE3_TO_UINT(buf);

			// read patch length
			fread(buf, 1, 2, f);
			Size = BYTE2_TO_UINT(buf);

			bRLE = (Size == 0);
			if (bRLE) {
				fread(buf, 1, 2, f);
				Size = BYTE2_TO_UINT(buf);
				ch = fgetc(f);
			}

			while (Size--) {
				// When in the read-only state, the only thing is to get nIpsMaxFileLen, thus avoiding memory out-of-bounds.
				if (!readonly) mem8 = base + Offset + nRomOffset;
                Offset++;
                if (readonly) {
                    if (!bRLE) fgetc(f);
                } else {
					*mem8 = bRLE ? ch : fgetc(f);
                }
			}

			if (Offset > nIpsMaxFileLen) nIpsMaxFileLen = Offset; // file size is growing
		}
	}

	fclose(f);
}

static char* stristr_int(const char* str1, const char* str2)
{
    const char* p1 = str1;
    const char* p2 = str2;
    const char* r = (!*p2) ? str1 : NULL;

    while (*p1 && *p2) {
        if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2)) {
            if (!r) {
                r = p1;
            }

            p2++;
        } else {
            p2 = str2;
            if (r) {
                p1 = r + 1;
            }

            if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2)) {
                r = p1;
                p2++;
            } else {
                r = NULL;
            }
        }

        p1++;
    }

    return (*p2) ? NULL : (char*)r;
}

static void DoPatchGame(const char* patch_name, char* game_name, UINT8* base, bool readonly)
{
	char s[MAX_PATH];
    char* p = NULL;
	char* rom_name = NULL;
	char* ips_name = NULL;
	char* ips_offs = NULL;
	FILE* fp = NULL;
	unsigned long nIpsSize;

    if ((fp = fopen(patch_name, "rb")) != NULL) {
		// get ips size
		fseek(fp, 0, SEEK_END);
		nIpsSize = ftell(fp);
		fseek(fp, 0, SEEK_SET);

        while (!feof(fp)) {
			if (fgets(s, sizeof(s), fp) != NULL) {
				p = s;

				// skip UTF-8 sig
				if (strncmp(p, UTF8_SIGNATURE, strlen(UTF8_SIGNATURE)) == 0)
					p += strlen(UTF8_SIGNATURE);

				if (p[0] == '[')	// '['
					break;

                // Can support linetypes:
                // "rom name.bin" "patch file.ips" CRC(abcd1234)
                // romname.bin patchfile CRC(abcd1234)

                if (p[0] == '\"') { // "quoted rom name with spaces.bin"
                    p++;
                    rom_name = strtok(p, "\"");
                } else {
                    rom_name = strtok(p, " \t\r\n");
                }

				if (!rom_name)
					continue;
				if (*rom_name == '#')
					continue;
				if (_stricmp(rom_name, game_name))
					continue;

                ips_name = strtok(NULL, "\t\r\n");

				if (ips_name[0] == '\t') ips_name++;

				if (!ips_name)
					continue;

				nRomOffset = 0; // Reset to 0

				if (NULL != (ips_offs = strtok(NULL, " \t\r\n"))) {
					if (ips_offs[0] == '\t') ips_offs++;

					if (0 == strcmp(ips_offs, "IPS_OFFSET_016")) {
						nRomOffset = IPS_OFFSET_016;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_032")) {
						nRomOffset = IPS_OFFSET_032;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_048")) {
						nRomOffset = IPS_OFFSET_048;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_064")) {
						nRomOffset = IPS_OFFSET_064;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_080")) {
						nRomOffset = IPS_OFFSET_080;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_096")) {
						nRomOffset = IPS_OFFSET_096;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_112")) {
						nRomOffset = IPS_OFFSET_112;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_128")) {
						nRomOffset = IPS_OFFSET_128;
					} else if (0 == strcmp(ips_offs, "IPS_OFFSET_144")) {
						nRomOffset = IPS_OFFSET_144;
					} else {
						nRomOffset = 0;
					}
				}

                // remove crc portion, and end quote/spaces from ips name
                char *c = stristr_int(ips_name, "crc");
                if (c) {
                    c--; // "derp.ips" CRC(abcd1234)\n"
                         //           ^ we're now here.
                    while (*c && (*c == ' ' || *c == '\t' || *c == '\"'))
                    {
                        *c = '\0';
                        c--;
                    }
                }

                // clean-up IPS name beginning (could be quoted or not)
                while (ips_name && (ips_name[0] == '\t' || ips_name[0] == ' ' || ips_name[0] == '\"'))
                    ips_name++;

                char *has_ext = stristr_int(ips_name, ".ips");

                bprintf(0, _T("ips name:[%S]\n"), ips_name);
                bprintf(0, _T("rom name:[%S]\n"), rom_name);

				char ips_path[MAX_PATH*2];
				char ips_dir[MAX_PATH];
				TCHARToANSI(szAppIpsPath, ips_dir, sizeof(ips_dir));

				if (strchr(ips_name, '\\')) {
					// ips in parent's folder
                    sprintf(ips_path, "%s\\%s%s", ips_dir, ips_name, (has_ext) ? "" : IPS_EXT);
				} else {
					sprintf(ips_path, "%s%s\\%s%s", ips_dir, BurnDrvGetTextA(DRV_NAME), ips_name, (has_ext) ? "" : IPS_EXT);
				}

				PatchFile(ips_path, base, readonly);
			}
		}
		fclose(fp);
	}
}

void IpsApplyPatches(UINT8* base, char* rom_name)
{
	char ips_data[MAX_PATH];

	nIpsMaxFileLen = 0;

	int nActivePatches = GetIpsNumActivePatches();

	for (int i = 0; i < nActivePatches; i++) {
		memset(ips_data, 0, MAX_PATH);
		TCHARToANSI(szIpsActivePatches[i], ips_data, sizeof(ips_data));
		DoPatchGame(ips_data, rom_name, base, false);
	}
}

UINT32 GetIpsDrvDefine()
{
	if (!bDoIpsPatch) return 0;

	UINT32 nRet = 0;

	char ips_data[MAX_PATH];
	int nActivePatches = GetIpsNumActivePatches();

	for (int i = 0; i < nActivePatches; i++) {
		memset(ips_data, 0, MAX_PATH);
		TCHARToANSI(szIpsActivePatches[i], ips_data, sizeof(ips_data));

		char str[MAX_PATH] = { 0 }, * ptr = NULL, * tmp = NULL;
		FILE* fp = NULL;

		if (NULL != (fp = fopen(ips_data, "rb"))) {
			while (!feof(fp)) {
				if (NULL != fgets(str, sizeof(str), fp)) {
					ptr = str;

					// skip UTF-8 sig
					if (0 == strncmp(ptr, UTF8_SIGNATURE, strlen(UTF8_SIGNATURE)))
						ptr += strlen(UTF8_SIGNATURE);

					if (NULL == (tmp = strtok(ptr, " \t\r\n")))
						continue;
					if (0 != strcmp(tmp, "#define"))
						continue;
					if (NULL == (tmp = strtok(NULL, " \t\r\n")))
						break;

					if (0 == strcmp(tmp, "IPS_NOT_PROTECT")) {
						nRet |= IPS_NOT_PROTECT;
						continue;
					}
					if (0 == strcmp(tmp, "IPS_PGM_SPRHACK")) {
						nRet |= IPS_PGM_SPRHACK;
						continue;
					}
					if (0 == strcmp(tmp, "IPS_PGM_SNDOFS6")) {
						nRet |= IPS_PGM_SNDOFS6;
						continue;
					}
					if (0 == strcmp(tmp, "IPS_PGM_MAPHACK")) {
						nRet |= IPS_PGM_MAPHACK;
						continue;
					}
					if (0 == strcmp(tmp, "IPS_LOAD_OFFSET")) {
						nRet |= IPS_LOAD_OFFSET;
						continue;
					}

					// Assignment is only allowed once
					if (!INCLUDE_NEOP3(nRet)) {
						if (0 == strcmp(tmp, "IPS_NEOP3_20000")) {
							nRet |= IPS_NEOP3_20000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_NEOP3_40000")) {
							nRet |= IPS_NEOP3_40000;
							continue;
						}
					}
					if (!INCLUDE_PROG(nRet)) {
						if (0 == strcmp(tmp, "IPS_PROG_100000")) {
							nRet |= IPS_PROG_100000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_200000")) {
							nRet |= IPS_PROG_200000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_300000")) {
							nRet |= IPS_PROG_300000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_400000")) {
							nRet |= IPS_PROG_400000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_500000")) {
							nRet |= IPS_PROG_500000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_600000")) {
							nRet |= IPS_PROG_600000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_700000")) {
							nRet |= IPS_PROG_700000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_800000")) {
							nRet |= IPS_PROG_800000;
							continue;
						}
						if (0 == strcmp(tmp, "IPS_PROG_900000")) {
							nRet |= IPS_PROG_900000;
							continue;
						}
					}
				}
			}
			fclose(fp);
		}
	}

	return nRet;
}

INT32 GetIpsesMaxLen(char* rom_name)
{
	INT32 nRet = -1;	// The function returns the last patched address if it succeeds, and -1 if it fails.

	if (NULL != rom_name) {
		char ips_data[MAX_PATH];
		nIpsMaxFileLen = 0;
		int nActivePatches = GetIpsNumActivePatches();

		for (int i = 0; i < nActivePatches; i++) {
			memset(ips_data, 0, MAX_PATH);
			TCHARToANSI(szIpsActivePatches[i], ips_data, sizeof(ips_data));
			DoPatchGame(ips_data, rom_name, NULL, true);
			if (nIpsMaxFileLen > nRet) nRet = nIpsMaxFileLen;	// Returns the address with the largest length in ipses.
		}
	}

	if (GetIpsDrvDefine() & IPS_LOAD_OFFSET) nRet += 0x800000;

	return nRet;
}

void IpsPatchExit()
{
	bDoIpsPatch = false;
}
