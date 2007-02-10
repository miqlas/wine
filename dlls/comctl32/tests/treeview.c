/* Unit tests for treeview.
 *
 * Copyright 2005 Krzysztof Foltman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "winreg.h"
#include "commctrl.h" 

#include "wine/test.h"

static HWND hMainWnd;

static HWND hTree, hEdit;
static HTREEITEM hRoot, hChild;

static int pos = 0;
static char sequence[256];

static void Clear(void)
{
    pos = 0;
    sequence[0] = '\0';
}

static void AddItem(char ch)
{
    sequence[pos++] = ch;
    sequence[pos] = '\0';
}

static void IdentifyItem(HTREEITEM hItem)
{
    if (hItem == hRoot) {
        AddItem('R');
        return;
    }
    if (hItem == hChild) {
        AddItem('C');
        return;
    }
    if (hItem == NULL) {
        AddItem('n');
        return;
    }
    AddItem('?');
}

static void FillRoot(void)
{
    TVINSERTSTRUCTA ins;
    TVITEM tvi;
    static CHAR root[]  = "Root",
                child[] = "Child";

    Clear();
    AddItem('A');
    ins.hParent = TVI_ROOT;
    ins.hInsertAfter = TVI_ROOT;
    U(ins).item.mask = TVIF_TEXT;
    U(ins).item.pszText = root;
    hRoot = TreeView_InsertItem(hTree, &ins);
    assert(hRoot);

    /* UMLPad 1.15 depends on this being not -1 (I_IMAGECALLBACK) */
    tvi.hItem = hRoot;
    tvi.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    SendMessage( hTree, TVM_GETITEM, 0, (LPARAM)&tvi );
    ok(tvi.iImage == 0, "tvi.iImage=%d\n", tvi.iImage);
    ok(tvi.iSelectedImage == 0, "tvi.iSelectedImage=%d\n", tvi.iSelectedImage);

    AddItem('B');
    ins.hParent = hRoot;
    ins.hInsertAfter = TVI_FIRST;
    U(ins).item.mask = TVIF_TEXT;
    U(ins).item.pszText = child;
    hChild = TreeView_InsertItem(hTree, &ins);
    assert(hChild);
    AddItem('.');

    ok(!strcmp(sequence, "AB."), "Item creation\n");
}

static void DoTest1(void)
{
    BOOL r;
    r = TreeView_SelectItem(hTree, NULL);
    Clear();
    AddItem('1');
    r = TreeView_SelectItem(hTree, hRoot);
    AddItem('2');
    r = TreeView_SelectItem(hTree, hRoot);
    AddItem('3');
    r = TreeView_SelectItem(hTree, NULL);
    AddItem('4');
    r = TreeView_SelectItem(hTree, NULL);
    AddItem('5');
    r = TreeView_SelectItem(hTree, hRoot);
    AddItem('.');
    ok(!strcmp(sequence, "1(nR)nR23(Rn)Rn45(nR)nR."), "root-none select test\n");
}

static void DoTest2(void)
{
    BOOL r;
    r = TreeView_SelectItem(hTree, NULL);
    Clear();
    AddItem('1');
    r = TreeView_SelectItem(hTree, hRoot);
    AddItem('2');
    r = TreeView_SelectItem(hTree, hRoot);
    AddItem('3');
    r = TreeView_SelectItem(hTree, hChild);
    AddItem('4');
    r = TreeView_SelectItem(hTree, hChild);
    AddItem('5');
    r = TreeView_SelectItem(hTree, hRoot);
    AddItem('.');
    ok(!strcmp(sequence, "1(nR)nR23(RC)RC45(CR)CR."), "root-child select test\n");
}

static void DoFocusTest(void)
{
    TVINSERTSTRUCTA ins;
    static CHAR child1[]  = "Edit",
                child2[]  = "A really long string";
    HTREEITEM hChild1, hChild2;

    /* This test verifies that when a label is being edited, scrolling
     * the treeview does not cause the label to lose focus. To test
     * this, first some additional entries are added to generate
     * scrollbars.
     */
    ins.hParent = hRoot;
    ins.hInsertAfter = hChild;
    U(ins).item.mask = TVIF_TEXT;
    U(ins).item.pszText = child1;
    hChild1 = TreeView_InsertItem(hTree, &ins);
    assert(hChild1);
    ins.hInsertAfter = hChild1;
    U(ins).item.mask = TVIF_TEXT;
    U(ins).item.pszText = child2;
    hChild2 = TreeView_InsertItem(hTree, &ins);
    assert(hChild2);

    ShowWindow(hMainWnd,SW_SHOW);
    SendMessageW(hTree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hChild);
    hEdit = TreeView_EditLabel(hTree, hChild);
    ScrollWindowEx(hTree, -10, 0, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN);
    ok(GetFocus() == hEdit, "Edit control should have focus\n");
}

static LRESULT CALLBACK MyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg) {

    case WM_CREATE:
    {
        hTree = CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, NULL, WS_CHILD|WS_VISIBLE|
            TVS_LINESATROOT|TVS_HASLINES|TVS_HASBUTTONS|TVS_EDITLABELS,
            0, 0, 120, 100, hWnd, (HMENU)100, GetModuleHandleA(0), 0);

        SetFocus(hTree);
        return 0;
    }
    case WM_NOTIFY:
    {
        NMHDR *pHdr = (NMHDR *)lParam;
    
        if (pHdr->idFrom == 100) {
            NMTREEVIEWA *pTreeView = (LPNMTREEVIEWA) lParam;
            switch(pHdr->code) {
            case TVN_SELCHANGINGA:
                AddItem('(');
                IdentifyItem(pTreeView->itemOld.hItem);
                IdentifyItem(pTreeView->itemNew.hItem);
                return 0;
            case TVN_SELCHANGEDA:
                AddItem(')');
                IdentifyItem(pTreeView->itemOld.hItem);
                IdentifyItem(pTreeView->itemNew.hItem);
                return 0;
            }
        }
        return 0;
    }
  
    case WM_SIZE:
        MoveWindow(hTree, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        break;
      
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
  
    default:
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
    return 0L;
}

START_TEST(treeview)
{
    WNDCLASSA wc;
    MSG msg;
    INITCOMMONCONTROLSEX icex;
    RECT rc;
  
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC   = ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icex);
  
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hIcon = NULL;
    wc.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(IDC_IBEAM));
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "MyTestWnd";
    wc.lpfnWndProc = MyWndProc;
    RegisterClassA(&wc);


    hMainWnd = CreateWindowExA(0, "MyTestWnd", "Blah", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 130, 105, NULL, NULL, GetModuleHandleA(NULL), 0);
    GetClientRect(hMainWnd, &rc);

    FillRoot();
    DoTest1();
    DoTest2();
    DoFocusTest();

    PostMessageA(hMainWnd, WM_CLOSE, 0, 0);
    while(GetMessageA(&msg,0,0,0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}
