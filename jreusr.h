// begin of jreusr.h
// ���C�u�����̎g�p�җp�w�b�_�t�@�C��

#ifndef		JREUSR
#define		JREUSR

// ---------------------------------------------------
// Prevent multiple includes.

// _os_os2�͂���(jreusr.h)���C���N���[�h���郆�[�U�[����`����
#if defined(_OS_OS2)
// in case of os/2
	// __OS2_H__ is for bc 2.0 e
	// __OS2_H__ is for bc 1.5 e
	#if !defined(__IBMC__) || !defined(__OS2_H__)
		// ���̃N���X��type.h���C���N���[�h�����Ȃ��悤�ɐ��include����.
		// INCL_???�ȂǕK�v�Ȓ萔�͐�ɒ�`���Ă���.
		#include <os2.h>
	#endif	// defined(__IBMC__) || defined(__OS2_H__)

	#define JRE_BUILDTYPE_OS2PM

#else
// in case of windows

	// bc4.5 e __WINDOWS_H,
	// bc4.0 j __WINDOWS_H,
	// bc3.1 j __WINDOWS_H,
	// msvc4.1 j _INC_WINDOWS, _WINDOWS_
	// msvc4.0 j _INC_WINDOWS,
	// msvc2.0 j ???, ???
	// msvc1.0 j _INC_WINDOWS,
	// ddk _INC_WINDOWS
	// WinCE SDK - WIN32 and PEGASUS

	#if !defined(__WINDOWS_H) && !defined(_INC_WINDOWS)
		#ifndef STRICT
			#define STRICT
		#endif	// STRICT
		// ���̃N���X��type.h���C���N���[�h�����Ȃ��悤�ɐ��include����.
		#include <windows.h>
	#endif	// !defined(__WINDOWS_H) && !defined(_INC_WINDOWS)

	#if defined(__WIN32__) || defined(WIN32)
		#define JRE_BUILDTYPE_WIN32
		#if defined(PEGASUS)
			#define JRE_SUBSET_WINCE
		#endif
	#else
		#define JRE_BUILDTYPE_WIN16
	#endif
	#define JRE_BUILDTYPE_WINDOWS

#endif	// os/2 or windows

// ---------------------------------------------------
#if defined(JRE_BUILDTYPE_OS2PM)
	// in case of JRE_BUILDTYPE_OS2PM
	// �ȉ��Cjre/os2���[�J���̒�`
	#if !defined(EXTAPI)
		#define EXTAPI _export EXPENTRY
	#endif	// !defined(EXTAPI)
	#if !defined(MEMID)
		typedef void* MEMID;
	#endif	// !defined(MEMID)

	// os2�ɂ�PSZ������.
	typedef PSZ PSTR, LPSTR;
	typedef int (*FARPROC)(void);	// farproc
	//os2�ɂ�USHORT�CULONG������.
	typedef USHORT WORD;
	typedef ULONG DWORD;
	// OS/2�ɂ�SHANDLE��LHANDLE������.
	typedef LHANDLE HANDLE;	// �ėp�n���h��������Ă����āc�c
	typedef HANDLE GLOBALHANDLE;
	typedef HANDLE*PHANDLE, *LPHANDLE;

#else
	// in case of JRE_BUILDTYPE_WIN32 or JRE_BUILDTYPE_WIN16 (= JRE_BUILDTYPE_WINDOWS)
	// �ȉ��Cjre/win���[�J���̒�`
	#if !defined(EXTAPI)
		#if defined(_MSC_VER)
			#define EXTAPI WINAPI	// VC2�ł�_export���ʂ�Ȃ��̂Œ���.
		#endif
		#if defined(__BORLANDC__)
			#if defined(_TEST_EXE)
				#define EXTAPI WINAPI
			#else
				#define EXTAPI _export WINAPI
			#endif
		#endif
	#endif	// !defined(EXTAPI)

	#if !defined(MEMID)
		#if defined(JRE_BUILDTYPE_WIN32)
			typedef void*MEMID;	// �����I�Ƀ|�C���^�ɂ��܂���.
		#else
			typedef HGLOBAL MEMID;
		#endif	// __WIN32__
	#endif	// !defined(MEMID)

#endif

// --------------------------------------------------- �O���[�o���ϐ�



// ------------------------------------------------------------- �萔
// _JRE_ERR_CODE��_JRE_WARN_CODE�̃V���{���͎R�c���f�o�b�O���Ɏg�p���Ă��܂�.

#define _JRE_ERR_CODE
// �G���[�R�[�h.nError��1�`63�܂�. int�^
#define		CantAllocate	( 1)	//	�������̊m�ۂ��ł��Ȃ�.��ΓI�ȃ������s��.
#define		MemoryTooFew1	( 2)	//	������������Ȃ�1.(���s���Ɍ��܂����.�������T�C�Y�̗\���Ɏ��s����)
#define		MemoryTooFew2	( 3)	//  ������������Ȃ�2.(DLL�쐬���Ɍ��܂����)
#define		ReTooLong		(10)	//	���K�\������������.
#define		TooComplex		(13)	//	�����ƊȒP�ȕ\���ɂ��ĉ�����.����DLL�̎ア�p�^�[����,�������Ȃ�.�������ő��Ȏ��ł͔������Ȃ�.
#define		MismatchBracket	(20)	//	���ʂ̑Ή����������Ȃ�.
#define		InvalidChClass	(21)	//	�L�����N�^�N���X�̓��e�����߂ł��Ȃ�.�w�ǂ̏ꍇ�͈͎w�肪�������Ȃ�.
#define		EscErr			(24)	//	�G�X�P�[�v�V�[�N�F���X�����߂ł��Ȃ�.
#define		Unknown			(31)	//  �Ȃ񂾂��ǂ��킩��Ȃ��G���[.�����I�ȗv���Ŕ�������G���[.�w�ǂ��������T�C�Y�̗\���Ɏ��s.
#define		NoReString		(32)	//  �����p�^�[�����w�肵�ĉ�����.���K�\������0�o�C�g.
#define		IncorrectUsing	(33)	//  �p�����[�^����������.DLL�̕s���Ȏg�p�@.
#define		ReNotExist		(34)	//	��������O�ɃR���p�C�����ĉ�����.
#define		InternalErr		(35)	//  DLL�̃o�O�����o����.���ꂪ�����������҂ɘA�����ė~����.
#define		UsrAbort		(36)	//  ���[�U�[(�A�v���P�[�V����)�ɂ�钆�f.
#define		OldVersion		(37)	//	�Â�(���߂ł��Ȃ�)�o�[�W�����̌Ăяo���菇���g�p����.
// CantAllocate, TooComplex, MemoryTooFew2, ReNotExist, IncorrectUsing, UsrAbort�͌������ɂ������������

#define _JRE_WARN_CODE
// �x���R�[�h. ���|�[�g�R�[�h. jre2�\���̂�nWarning�����o.
// ����͂��ꂼ��̃r�b�g�Ƀ}�b�s���O����\���������̂ŁC
// if (CwInlinePattern | jre2.nWarning){
// }
// �ŕ]�����Ă�������.
#define		CwInlinePattern	(2)	// �s���̏����ɂ��C�ČĂяo���̕K�v�͂Ȃ�.

#define JGC_SHORT (1)
#define JGC_LONG (2)

// jre.dll�Ŏg�p���郁�b�Z�[�W�̍ő咷(�ۏ�).���̃T�C�Y�̃o�b�t�@�Ɏ��܂�Ȃ�������͓n���܂���(null�܂Ŋ܂߂Ă��̃T�C�Y).
#define JRE_MAXLEN (128)

// GetJreMessage�̌���ԍ�.
#define GJM_JPN (0)
#define GJM_ENG (1)

// ----------------------------------------------------------- �\����
// JRE�\����. �o�[�W�����ɂ�����炸jre�\���̂̃A���C�������g��8bit(1byte)�ł��B
#pragma pack(1)
// JRE�\���̂͋ɗ͎g�p���Ȃ��ł�������.������ް�ޮ݂Ŕp�~���܂�(�o�[�W����2.xx�܂łŻ�߰Ă��~�߂܂�).
// ����ɑ���\���̂�JRE2�\���̂ł�.
typedef struct tagJRE{
	BOOL bConv;						// ���̍\���̂̎g�p���������t���O.
	int nStart;						// �����J�n�ʒu.�o�C�g��.�擪��0.
	int nWarning;					// �E�H�[�j���O�R�[�h.
	int nError;						// �G���[�R�[�h.
	int nLength;					// �}�b�`��.�o�C�g��.
	int nPosition;					// �}�b�`�ʒu.�擪��0.(���p��������)
	WORD wTranslate;				// �ϊ��e�[�u���ԍ�.
	LPSTR lpszTable;				// �ϊ��e�[�u��.
	FARPROC lpfnUsrFunc;			// �R�[���o�b�N�֐��ւ�FAR�|�C���^.
	int nCompData1;					// �R���p�C���f�[�^1.
	MEMID hCompData2;		// �R���p�C���f�[�^2.�n���h��.
	MEMID hCompData3;		// �R���p�C���f�[�^3.�n���h��.
	MEMID hCompData4;		// �R���p�C���f�[�^4.�n���h��.
} JRE, *PJRE, NEAR*NPJRE, FAR*LPJRE;
#pragma pack()


#pragma pack(1)
// �o�[�W����1.06�ȍ~�͂��̍\���̂��g�p���Ă�������.
// ������,�o�[�W����1.xx�ł͎d�l�������I�ł�(�ǂȂ����[���イ�˂�).
typedef struct tagJRE2{
	DWORD dwSize;					// ���̍\���̂̃T�C�Y.
	BOOL bConv;						// ���̍\���̂̎g�p���������t���O.
	UINT nStart;					// �����J�n�ʒu.�o�C�g��.�擪��0.
	UINT nWarning;					// �E�H�[�j���O�R�[�h. (v1.11����UINT�ɕύX)
	int nError;						// �G���[�R�[�h.
	UINT nLength;					// �}�b�`��.�o�C�g��.
	UINT nPosition;					// �}�b�`�ʒu.�擪��0.(���p��������)
	WORD wTranslate;				// �ϊ��e�[�u���ԍ�.
	LPSTR lpszTable;				// �ϊ��e�[�u��.
	FARPROC lpfnUsrFunc;			// �R�[���o�b�N�֐��ւ�FAR�|�C���^(�g��Ȃ��悤��).
	UINT nCompData1;					// �R���p�C���f�[�^1.
	MEMID hCompData2;		// �R���p�C���f�[�^2.�n���h��.
	MEMID hCompData3;		// �R���p�C���f�[�^3.�n���h��.
	MEMID hCompData4;		// �R���p�C���f�[�^4.�n���h��.
} JRE2, *PJRE2, NEAR*NPJRE2, FAR*LPJRE2;
#pragma pack()

// ------------------------------------------------ �������߂����ϸ�
#ifdef __cplusplus	// caution! it's NOT cpulspuls!!!
	extern "C"{
#endif	// __cplusplus

// �o�[�W�����Ɋ֌W�Ȃ�API
BOOL EXTAPI IsMatch(LPSTR lpszStr, LPSTR lpszRe);
int EXTAPI GlobalReplace(LPSTR lpszRe, LPSTR lpszObj, LPSTR lpszStr, LPHANDLE lphGMemTo);
WORD EXTAPI JreGetVersion(void);
#if defined(JRE_BUILDTYPE_WIN16)
	MEMID EXTAPI DecodeEscSeqAlloc2(LPSTR lpszRe);	// 16bit Windows�̈╨.
#endif	// defined(JRE_BUILDTYPE_WIN16)
UINT EXTAPI DecodeEscSeq(LPSTR lpszRe, LPSTR lpszBuff, UINT uiSize);	// new!
int EXTAPI GetJreMessage(int nMessageType, int nLanguage, LPSTR lpszBuff, int cbBuff);
// �o�[�W����1API
BOOL EXTAPI JreOpen(LPJRE lpjreJre);
BOOL EXTAPI JreCompile(LPJRE lpjreJre, LPSTR lpszRe);
BOOL EXTAPI JreGetMatchInfo(LPJRE lpjreJre, LPSTR lpszStr);
BOOL EXTAPI JreClose(LPJRE lpjreJre);
// �o�[�W����2API
BOOL EXTAPI Jre2Open(LPJRE2 lpjreJre);
BOOL EXTAPI Jre2Compile(LPJRE2 lpjreJre, LPSTR lpszRe);
BOOL EXTAPI Jre2GetMatchInfo(LPJRE2 lpjreJre, LPSTR lpszStr);
BOOL EXTAPI Jre2Close(LPJRE2 lpjreJre);


#ifdef __cplusplus
	}
#endif	// __cplusplus

// ----------------------------------------------------------- �^��`
// GetProcAddress���g�p����ۂ̃|�C���^�ϐ���錾���₷�����Ă���.
typedef BOOL (EXTAPI*LPISMATCH) (LPSTR, LPSTR);
typedef int (EXTAPI*LPGLOBALREPLACE) (LPSTR, LPSTR, LPSTR, LPHANDLE);
typedef WORD (EXTAPI*LPJREGETVERSION) (VOID);
#if defined(JRE_BUILDTYPE_WIN16)
	typedef MEMID (EXTAPI*LPDECODEESCSEQALLOC2) (LPSTR);
#endif	// defined(JRE_BUILDTYPE_WIN16)
typedef UINT (EXTAPI*LPDECODEESCSEQ) (LPSTR lpszRe, LPSTR lpszBuff, UINT uiSize);
typedef int (EXTAPI*LPGETJREMESSAGE) (int, int, LPSTR, int);
typedef BOOL (EXTAPI*LPJRE2OPEN) (LPJRE2);
typedef BOOL (EXTAPI*LPJRE2COMPILE) (LPJRE2, LPSTR);
typedef BOOL (EXTAPI*LPJRE2GETMATCHINFO) (LPJRE2, LPSTR);
typedef BOOL (EXTAPI*LPJRE2CLOSE) (LPJRE2);




#endif	// JREUSR
// end of jreusr.h