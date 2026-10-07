/*
 * 만들어둔 플로우차트를 기반으로 작성된 구현 코드
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team 2",
    /* First member's full name */
    "Moohun Choi",
    /* First member's email address */
    "271946661+choimoohun@users.noreply.github.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

// 상수
enum {
    WSIZE = 4,                                      // 4B: 헤더 또는 푸터 하나의 크기
    DSIZE = 8,                                      // 8B: 헤더와 푸터의 합
    MIN_BLOCK = DSIZE << 1,                         // 16B: 일반 블록의 최소 크기
    CHUNKSIZE = 1 << 12                             // 4096B: 기본 힙 확장량
};

// 크기 비교: 더 큰 값을 반환
static inline size_t MAX(size_t a, size_t b)
{
    return a > b ? a : b;
}

// 헤더와 푸터를 포함하여 8의 배수로 정렬
static inline size_t ALIGN(size_t size)
{
    return (size + DSIZE + 7) & ~((size_t)7);
}

static void *heap_listp = NULL; // 프롤로그의 bp를 가리키는 전역 스칼라 포인터
static void *lastp = NULL;      // 마지막으로 탐색한 페이로드를 가리키는 전역 스칼라 포인터

// ======================
// 포인터가 가리키는 워드를 읽거나 쓰는 역할을 한다. (1워드 = 4바이트 기준)
// ======================

// 블록 메타데이터 읽기
static inline unsigned int GET(const void *p)
{
	return *(const unsigned int *)p;
}

/// @brief 블록 메타데이터를 기록한다. PACK()과 같이 사용할 것.
/// @param p 
/// @param value 
static inline void PUT(void *p, size_t value)
{
	*(unsigned int *)p = value;
}

// ======================
// 블록 메타데이터
// ======================

/// @brief 크기와 할당 비트를 통합하여 메타데이터에 저장할 수 있도록 함.
/// @param size 블록의 크기
/// @param a 할당 여부를 나타내는 비트 (반드시 0과 1 중 하나)
/// @return 통합된 블록 메타데이터
static inline unsigned int PACK(size_t size, unsigned int a)
{
	return (unsigned int)size | a;
}

// 크기 읽기: 반드시 헤더나 푸터를 가리키는 포인터를 인자로 넣을 것
static inline size_t GET_SIZE(const void *p)
{
	return (size_t)(GET(p) & ~0x7u);
}

// 가용 상태 확인: 반드시 헤더나 푸터를 가리키는 포인터를 인자로 넣을 것
static inline unsigned int GET_ALLOC(const void *p)
{
	return GET(p) & 0x1u;
}

// ======================
// 블록 탐색: 포인터 이동
// ======================

// 헤더를 가리키는 포인터
static inline char *HDRP(void *bp)
{
	return (char *)bp - WSIZE;
}

// 푸터를 가리키는 포인터
static inline char *FTRP(void *bp)
{
	return (char *)bp + GET_SIZE(HDRP(bp)) - DSIZE;
}

// 다음 블록의 bp
static inline char *NEXT_BLKP(void *bp)
{
	return (char *)bp + GET_SIZE(HDRP(bp));
}

// 이전 블록의 bp
static inline char *PREV_BLKP(void *bp)
{
	return (char *)bp - GET_SIZE((char *)bp - DSIZE);
}

// 새 영역을 정상 블록으로 바꾸기
static void *extend_heap(size_t bytes);

// 초기화된 힙을 만든다. 가용 블럭이 없는 빈 상태.
int mm_init(void)
{
    heap_listp = NULL;
    lastp = NULL;
    char *base = mem_sbrk(4 * WSIZE);   // 패딩, 프롤로그의 헤더와 푸터, 에필로그의 영역 추가

    if (base == (void *)-1)
    {
        return -1;
    }

    PUT(base, 0);                           // 정렬 패딩
    PUT(base + WSIZE, PACK(DSIZE, 1));      // 프롤로그 헤더
    PUT(base + 2 * WSIZE, PACK(DSIZE, 1));  // 프롤로그 푸터
    PUT(base + 3 * WSIZE, PACK(0, 1));      // 에필로그 헤더

    // heap_listp를 프롤로그의 헤더와 푸터 사이에 위치
    heap_listp = base + 2 * WSIZE;
    lastp = heap_listp + DSIZE;

    if (extend_heap(CHUNKSIZE) == NULL)
    {
        return -1;
    }
    
    return 0;
}

static void *coalesce(void *bp)
{
    unsigned int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));   // 이전 블록 푸터의 할당 비트
    unsigned int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));   // 다음 블록 헤더의 할당 비트
    size_t size = GET_SIZE(HDRP(bp));                           // 현재 블록의 전체 바이트 수

    if (prev_alloc)
    {
        if (next_alloc)
        {
            // Case 1: 양쪽이 이미 다 할당되었으므로 바로 반환
        }
        else
        {
            // Case 2: 앞은 할당되었고 뒤는 가용 상태인 경우
            size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
            if (lastp == NEXT_BLKP(bp))
            {
                lastp = bp;
            }
            PUT(HDRP(bp), PACK(size, 0));                       // 현재 헤더에 합친 크기 기록
            PUT(FTRP(bp), PACK(size, 0));                       // 새 헤더 크기로 계산한 푸터에 기록
        }
    }
    else
    {
        if (next_alloc)
        {
            // Case 3: 앞은 가용 상태이고 뒤는 할당 상태인 경우
            size += GET_SIZE(HDRP(PREV_BLKP(bp)));
            if (lastp == bp)
            {
                lastp = PREV_BLKP(bp);
            }
            PUT(FTRP(bp), PACK(size, 0));                       // 기존 현재 블록의 푸터
            PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));            // 이전 블록의 헤더
            bp = PREV_BLKP(bp);                                 // 병합 결과의 첫 페이로드 주소
        }
        else
        {
            // Case 4: 양쪽이 모두 가용 상태인 경우
            size += GET_SIZE(HDRP(PREV_BLKP(bp)));
            size += GET_SIZE(FTRP(NEXT_BLKP(bp)));
            if (lastp == bp || lastp == NEXT_BLKP(bp))
            {
                lastp = PREV_BLKP(bp);
            }
            PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));            // 이전 블록의 헤더
            PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));            // 다음 블록의 푸터
            bp = PREV_BLKP(bp);                                 // 병합 결과의 첫 페이로드 주소
        }
    }
    return bp;
}

static void *extend_heap(size_t bytes)
{
    // 확장량 계산과 메모리 확보
    if (bytes == 0)
    {
        return NULL;
    }

    size_t size = (bytes + 7) & ~(size_t)7; // 8B 단위로 올림
    size = MAX(MIN_BLOCK, size);

    void *bp = mem_sbrk((int)size);

    if (bp == (void *)-1)
    {
        return NULL;
    }

    // 새 경계 기록과 병합
    PUT(HDRP(bp), PACK(size, 0));           // 옛 에필로그를 새 가용헤더로 덮어씀
    PUT(FTRP(bp), PACK(size, 0));           // 새 헤더의 size로 푸터 주소 계산
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // 새 에필로그 헤더
    return coalesce(bp);                    // 이전 블록과 병합한 결과 주소 반환
}

// first fit에서 수정: next fit 검색을 수행하는 find_fit
static void *find_fit(size_t asize)
{
    void *bp = GET_SIZE(HDRP(lastp)) ? lastp : NEXT_BLKP(heap_listp);   // 마지막으로 탐색한 위치부터 시작
    char trigger = 0;

    while (true)
    {
        // bp가 마지막으로 탐색한 블록에 도달하면 종료
        if (bp == lastp && trigger)
        {
            break;
        }
        if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize)
        {
            lastp = bp;
            return bp;
        }
        if (!GET_SIZE(HDRP(bp)))
        {
            bp = heap_listp;
        }
        bp = NEXT_BLKP(bp);
        trigger = 1;
    }

    return NULL;
}

// 연습문제 9.9: 할당 표시와 필요한 경우 분할
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));      // 현재 블록의 크기
    size_t rsize = csize - asize;           // 남는 바이트 수

    if (rsize >= MIN_BLOCK)
    {
        PUT(HDRP(bp), PACK(asize, 1));      // 앞 할당 블록의 헤더
        PUT(FTRP(bp), PACK(asize, 1));      // 축소된 앞 블록의 푸터
        char *next_bp = NEXT_BLKP(bp);      // 앞 헤더를 바꾼 뒤 계산
        PUT(HDRP(next_bp), PACK(rsize, 0)); // 뒤 가용 블록의 헤더
        PUT(FTRP(next_bp), PACK(rsize, 0)); // 뒤 헤더를 기록한 뒤 푸터 계산
    }
    else
    {
        // 블록 전체를 할당
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

// 모든 할당 단계 연결
void *mm_malloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
    
    size_t asize = ALIGN(size);
    asize = MAX(MIN_BLOCK, asize);
    
    void *bp = find_fit(asize);

    if (!bp)
    {
        size_t extendsize = MAX(asize, CHUNKSIZE);
        bp = extend_heap(extendsize);   // 바이트 수를 전달
    
        if (!bp)
        {
            return NULL;
        }
    }

    place(bp, asize);

    return bp;  // 병합 후 블록의 페이로드 주소
}

// 반환 표시 후 병합
void mm_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);   // 반환 포인터는 사용하지 않음
}

// 제자리 할당과 주소 변경이 구현된 재할당
// TODO: 재할당 과정에서 생기는 내부 단편화 해결해보기(언젠가)
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    size_t csize = GET_SIZE(HDRP(ptr));
    size_t asize = ALIGN(size);
    asize = MAX(MIN_BLOCK, asize);

    // 1. 현재 블록 자체가 이미 충분히 크면 새로 할당하지 않기
    if (csize >= asize)
    {
        return ptr;
    }
    // 2. 다음 블록이 가용 상태고, 합치기 충분하다면 제자리 확장
    else if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && (csize + GET_SIZE(HDRP(NEXT_BLKP(ptr)))) >= asize)
    {
        char *next_block = NEXT_BLKP(ptr);
        size_t newsize = csize + GET_SIZE(HDRP(next_block));
        PUT(HDRP(ptr), PACK(newsize, 1));
        PUT(FTRP(next_block), PACK(newsize, 1));
        return ptr;
    }
    // 3. 그 다음 블록이 epilogue라면 heap을 확장해서 현재 블록을 늘리기
    else if (!GET_SIZE(HDRP(NEXT_BLKP(ptr))))
    {
        void *nextptr = extend_heap(asize - csize);

        if (!nextptr)
        {
            return NULL;
        }

        size_t newsize = csize + GET_SIZE(HDRP(nextptr));
        PUT(HDRP(ptr), PACK(newsize, 1));
        PUT(FTRP(nextptr), PACK(newsize, 1));
        return ptr;
    }

    // 주소 변경
    void *newptr = mm_malloc(size);
    if (!newptr)
    {
        return NULL;
    }

    size_t copySize = csize - DSIZE;    // 기존 블록의 페이로드 크기
    if (size < copySize)
    {
        copySize = size;
    }
    
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);

    return newptr;
}