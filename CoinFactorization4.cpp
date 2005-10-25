// Copyright (C) 2002, International Business Machines
// Corporation and others.  All Rights Reserved.

#if defined(_MSC_VER)
// Turn off compiler warning about long names
#  pragma warning(disable:4786)
#endif

#include "CoinFactorization.hpp"
#include "CoinIndexedVector.hpp"
#include "CoinHelperFunctions.hpp"
#include <cassert>
#include <cstdio>

// For semi-sparse
#define BITS_PER_CHECK 8
#define CHECK_SHIFT 3
typedef unsigned char CoinCheckZero;


//  updateColumnU.  Updates part of column (FTRANU)
void
CoinFactorization::updateColumnU ( CoinIndexedVector * regionSparse,
				   int * indexIn) const
{
  int numberNonZero = regionSparse->getNumElements (  );

  int goSparse;
  // Guess at number at end
  if (sparseThreshold_>0) {
    if (ftranAverageAfterR_) {
      int newNumber = (int) (numberNonZero*ftranAverageAfterU_);
      if (newNumber< sparseThreshold_)
	goSparse = 2;
      else if (newNumber< sparseThreshold2_)
	goSparse = 1;
      else
	goSparse = 0;
    } else {
      if (numberNonZero<sparseThreshold_) 
	goSparse = 2;
      else
	goSparse = 0;
    }
  } else {
    goSparse=0;
  }
  switch (goSparse) {
  case 0: // densish
    updateColumnUDensish(regionSparse,indexIn);
    break;
  case 1: // middling
    updateColumnUSparsish(regionSparse,indexIn);
    break;
  case 2: // sparse
    updateColumnUSparse(regionSparse,indexIn);
    break;
  }
  if (collectStatistics_) 
    ftranCountAfterU_ += (double) regionSparse->getNumElements (  );
}

//  updateColumnUDensish.  Updates part of column (FTRANU)
void
CoinFactorization::updateColumnUDensish ( CoinIndexedVector * regionSparse,
					  int * indexIn) const
{
  double *region = regionSparse->denseVector (  );
  int * regionIndex = regionSparse->getIndices();
  double tolerance = zeroTolerance_;
  const CoinBigIndex *startColumn = startColumnU_;
  const int *indexRow = indexRowU_;
  const double *element = elementU_;
  int numberNonZero = 0;
  const int *numberInColumn = numberInColumn_;
  int i;
  const double *pivotRegion = pivotRegion_;

  for (i = numberU_-1 ; i >= numberSlacks_; i-- ) {
    double pivotValue = region[i];
    if (pivotValue) {
      region[i] = 0.0;
      if ( fabs ( pivotValue ) > tolerance ) {
	CoinBigIndex start = startColumn[i];
	const double * thisElement = element+start;
	const int * thisIndex = indexRow+start;

	CoinBigIndex j;
	for (j=numberInColumn[i]-1 ; j >=0; j-- ) {
	  int iRow0 = thisIndex[j];
	  double regionValue0 = region[iRow0];
	  double value0 = thisElement[j];
	  region[iRow0] = regionValue0 - value0 * pivotValue;
	}
	pivotValue *= pivotRegion[i];
	region[i]=pivotValue;
	regionIndex[numberNonZero++]=i;
      }
    }
  }
    
  // now do slacks
  double factor = slackValue_;
  if (factor==1.0) {
    for ( i = numberSlacks_-1; i>=0;i--) {
      double value = region[i];
      double absValue = fabs ( value );
      if ( value ) {
	region[i]=0.0;
	if ( absValue > tolerance ) {
	  region[i]=value;
	  regionIndex[numberNonZero++]=i;
	}
      }
    }
  } else {
    assert (factor==-1.0);
    // Could skew loop to pick up next one earlier
    // might improve pipelining
    for ( i = numberSlacks_-1; i>=0;i--) {
      double value = region[i];
      double absValue = fabs ( value );
      if ( value ) {
	region[i]=0.0;
	if ( absValue > tolerance ) {
	  region[i]=-value;
	  regionIndex[numberNonZero++]=i;
	}
      }
    }
  }
  regionSparse->setNumElements ( numberNonZero );
}
//  updateColumnU.  Updates part of column (FTRANU)
/*
  Since everything is in order I should be able to do a better job of
  marking stuff - think.  Also as L is static maybe I can do something
  better there (I know I could if I marked the depth of every element
  but that would lead to other inefficiencies.
*/
void
CoinFactorization::updateColumnUSparse ( CoinIndexedVector * regionSparse,
					 int * indexIn) const
{
  int numberNonZero = regionSparse->getNumElements (  );
  int *regionIndex = regionSparse->getIndices (  );
  double *region = regionSparse->denseVector (  );
  double tolerance = zeroTolerance_;
  const CoinBigIndex *startColumn = startColumnU_;
  const int *indexRow = indexRowU_;
  const double *element = elementU_;
  const double *pivotRegion = pivotRegion_;
  // use sparse_ as temporary area
  // mark known to be zero
  int * stack = sparse_;  /* pivot */
  int * list = stack + maximumRowsExtra_;  /* final list */
  CoinBigIndex * next = (CoinBigIndex *) (list + maximumRowsExtra_);  /* jnext */
  char * mark = (char *) (next + maximumRowsExtra_);
  int nList, nStack;
  int i , iPivot;
#ifdef COIN_DEBUG
  for (i=0;i<maximumRowsExtra_;i++) {
    assert (!mark[i]);
  }
#endif

  // move slacks to end of stack list
  int * putLast = stack+maximumRowsExtra_;
  int * put = putLast;

  const int *numberInColumn = numberInColumn_;
  nList = 0;
  for (i=0;i<numberNonZero;i++) {
    int kPivot=indexIn[i];
    if(!mark[kPivot]) {
      stack[0]=kPivot;
      CoinBigIndex j=startColumn[kPivot]+numberInColumn[kPivot]-1;
      nStack=0;
      while (nStack>=0) {
	/* take off stack */
	if (j>=startColumn[kPivot]) {
	  int jPivot=indexRow[j--];
	  /* put back on stack */
	  next[nStack] =j;
	  if (!mark[jPivot]) {
	    /* and new one */
	    kPivot=jPivot;
	    j = startColumn[kPivot]+numberInColumn[kPivot]-1;
	    stack[++nStack]=kPivot;
	    mark[kPivot]=1;
	    next[nStack]=j;
	  }
	} else {
	  /* finished so mark */
	  if (kPivot>=numberSlacks_) {
	    list[nList++]=kPivot;
	  } else {
	    // slack - put at end
	    assert (!numberInColumn[kPivot]);
	    --put;
	    *put=kPivot;
	  }
	  mark[kPivot]=1;
	  --nStack;
	  if (nStack>=0) {
	    kPivot=stack[nStack];
	    j=next[nStack];
	  }
	}
      }
    }
  }
  numberNonZero=0;
  for (i=nList-1;i>=0;i--) {
    iPivot = list[i];
    mark[iPivot]=0;
    double pivotValue = region[iPivot];
    region[iPivot]=0.0;
    if ( fabs ( pivotValue ) > tolerance ) {
      CoinBigIndex start = startColumn[iPivot];
      int number = numberInColumn[iPivot];
      
      CoinBigIndex j;
      for ( j = start; j < start+number; j++ ) {
	double value = element[j];
	int iRow = indexRow[j];
	region[iRow] -=  value * pivotValue;
      }
      pivotValue *= pivotRegion[iPivot];
      region[iPivot]=pivotValue;
      regionIndex[numberNonZero++]=iPivot;
    }
  }
  // slacks
  if (slackValue_==1.0) {
    for (;put<putLast;put++) {
      int iPivot = *put;
      mark[iPivot]=0;
      double pivotValue = region[iPivot];
      region[iPivot]=0.0;
      if ( fabs ( pivotValue ) > tolerance ) {
	region[iPivot]=pivotValue;
	regionIndex[numberNonZero++]=iPivot;
      }
    }
  } else {
    for (;put<putLast;put++) {
      int iPivot = *put;
      mark[iPivot]=0;
      double pivotValue = region[iPivot];
      region[iPivot]=0.0;
      if ( fabs ( pivotValue ) > tolerance ) {
	region[iPivot]=-pivotValue;
	regionIndex[numberNonZero++]=iPivot;
      }
    }
  }
  regionSparse->setNumElements ( numberNonZero );
}
//  updateColumnU.  Updates part of column (FTRANU)
/*
  Since everything is in order I should be able to do a better job of
  marking stuff - think.  Also as L is static maybe I can do something
  better there (I know I could if I marked the depth of every element
  but that would lead to other inefficiencies.
*/
void
CoinFactorization::updateColumnUSparsish ( CoinIndexedVector * regionSparse,
					   int * indexIn) const
{
  int *regionIndex = regionSparse->getIndices (  );
  // mark known to be zero
  int * stack = sparse_;  /* pivot */
  int * list = stack + maximumRowsExtra_;  /* final list */
  CoinBigIndex * next = (CoinBigIndex *) (list + maximumRowsExtra_);  /* jnext */
  CoinCheckZero * mark = (CoinCheckZero *) (next + maximumRowsExtra_);
  const int *numberInColumn = numberInColumn_;
  int i, iPivot;
#ifdef COIN_DEBUG
  for (i=0;i<maximumRowsExtra_;i++) {
    assert (!mark[i]);
  }
#endif

  int nMarked=0;
  int numberNonZero = regionSparse->getNumElements (  );
  double *region = regionSparse->denseVector (  );
  double tolerance = zeroTolerance_;
  const CoinBigIndex *startColumn = startColumnU_;
  const int *indexRow = indexRowU_;
  const double *element = elementU_;
  const double *pivotRegion = pivotRegion_;

  for (i=0;i<numberNonZero;i++) {
    iPivot=indexIn[i];
    int iWord = iPivot>>CHECK_SHIFT;
    int iBit = iPivot-(iWord<<CHECK_SHIFT);
    if (mark[iWord]) {
      mark[iWord] |= 1<<iBit;
    } else {
      mark[iWord] = 1<<iBit;
      stack[nMarked++]=iWord;
    }
  }
  numberNonZero = 0;
  // First do down to convenient power of 2
  CoinBigIndex jLast = (numberU_-1)>>CHECK_SHIFT;
  jLast = CoinMax((jLast<<CHECK_SHIFT),(CoinBigIndex) numberSlacks_);
  for (i = numberU_-1 ; i >= jLast; i-- ) {
    double pivotValue = region[i];
    region[i] = 0.0;
    if ( fabs ( pivotValue ) > tolerance ) {
      CoinBigIndex start = startColumn[i];
      const double * thisElement = element+start;
      const int * thisIndex = indexRow+start;
      
      CoinBigIndex j;
      for (j=numberInColumn[i]-1 ; j >=0; j-- ) {
	int iRow0 = thisIndex[j];
	double regionValue0 = region[iRow0];
	double value0 = thisElement[j];
	int iWord = iRow0>>CHECK_SHIFT;
	int iBit = iRow0-(iWord<<CHECK_SHIFT);
	if (mark[iWord]) {
	  mark[iWord] |= 1<<iBit;
	} else {
	  mark[iWord] = 1<<iBit;
	  stack[nMarked++]=iWord;
	}
	region[iRow0] = regionValue0 - value0 * pivotValue;
      }
      pivotValue *= pivotRegion[i];
      region[i]=pivotValue;
      regionIndex[numberNonZero++]=i;
    }
  }
  int k;
  int kLast = (numberSlacks_+BITS_PER_CHECK-1)>>CHECK_SHIFT;
  if (jLast>numberSlacks_) {
    // now do in chunks
    for (k=(jLast>>CHECK_SHIFT)-1;k>=kLast;k--) {
      unsigned int iMark = mark[k];
      if (iMark) {
	// something in chunk - do all (as imark may change)
	int iLast = k<<CHECK_SHIFT;
	i = iLast+BITS_PER_CHECK-1;
	for ( ; i >= iLast; i-- ) {
	  double pivotValue = region[i];
	  if (pivotValue) {
	    region[i] = 0.0;
	    if ( fabs ( pivotValue ) > tolerance ) {
	      CoinBigIndex start = startColumn[i];
	      const double * thisElement = element+start;
	      const int * thisIndex = indexRow+start;
	      
	      CoinBigIndex j;
	      for (j=numberInColumn[i]-1 ; j >=0; j-- ) {
		int iRow0 = thisIndex[j];
		double regionValue0 = region[iRow0];
		double value0 = thisElement[j];
		int iWord = iRow0>>CHECK_SHIFT;
		int iBit = iRow0-(iWord<<CHECK_SHIFT);
		if (mark[iWord]) {
		  mark[iWord] |= 1<<iBit;
		} else {
		  mark[iWord] = 1<<iBit;
		  stack[nMarked++]=iWord;
		}
		region[iRow0] = regionValue0 - value0 * pivotValue;
	      }
	      pivotValue *= pivotRegion[i];
	      region[i]=pivotValue;
	      regionIndex[numberNonZero++]=i;
	    }
	  }
	}
	mark[k]=0;
      }
    }
    i = (kLast<<CHECK_SHIFT)-1;
  }
  for ( ; i >= numberSlacks_; i-- ) {
    double pivotValue = region[i];
    region[i] = 0.0;
    if ( fabs ( pivotValue ) > tolerance ) {
      CoinBigIndex start = startColumn[i];
      const double * thisElement = element+start;
      const int * thisIndex = indexRow+start;
      
      CoinBigIndex j;
      for (j=numberInColumn[i]-1 ; j >=0; j-- ) {
	int iRow0 = thisIndex[j];
	double regionValue0 = region[iRow0];
	double value0 = thisElement[j];
	int iWord = iRow0>>CHECK_SHIFT;
	int iBit = iRow0-(iWord<<CHECK_SHIFT);
	if (mark[iWord]) {
	  mark[iWord] |= 1<<iBit;
	} else {
	  mark[iWord] = 1<<iBit;
	  stack[nMarked++]=iWord;
	}
	region[iRow0] = regionValue0 - value0 * pivotValue;
      }
      pivotValue *= pivotRegion[i];
      region[i]=pivotValue;
      regionIndex[numberNonZero++]=i;
    }
  }
  
  if (numberSlacks_) {
    // now do slacks
    double factor = slackValue_;
    if (factor==1.0) {
      // First do down to convenient power of 2
      CoinBigIndex jLast = (numberSlacks_-1)>>CHECK_SHIFT;
      jLast = jLast<<CHECK_SHIFT;
      for ( i = numberSlacks_-1; i>=jLast;i--) {
	double value = region[i];
	double absValue = fabs ( value );
	if ( value ) {
	  region[i]=0.0;
	  if ( absValue > tolerance ) {
	  region[i]=value;
	  regionIndex[numberNonZero++]=i;
	  }
	}
      }
      mark[jLast]=0;
      // now do in chunks
      for (k=(jLast>>CHECK_SHIFT)-1;k>=0;k--) {
	unsigned int iMark = mark[k];
	if (iMark) {
	  // something in chunk - do all (as imark may change)
	  int iLast = k<<CHECK_SHIFT;
	  i = iLast+BITS_PER_CHECK-1;
	  for ( ; i >= iLast; i-- ) {
	    double value = region[i];
	    double absValue = fabs ( value );
	    if ( value ) {
	      region[i]=0.0;
	      if ( absValue > tolerance ) {
		region[i]=value;
		regionIndex[numberNonZero++]=i;
	      }
	    }
	  }
	  mark[k]=0;
	}
      }
    } else {
      assert (factor==-1.0);
      // First do down to convenient power of 2
      CoinBigIndex jLast = (numberSlacks_-1)>>CHECK_SHIFT;
      jLast = jLast<<CHECK_SHIFT;
      for ( i = numberSlacks_-1; i>=jLast;i--) {
	double value = region[i];
	double absValue = fabs ( value );
	if ( value ) {
	  region[i]=0.0;
	  if ( absValue > tolerance ) {
	    region[i]=-value;
	    regionIndex[numberNonZero++]=i;
	  }
	}
      }
      mark[jLast]=0;
      // now do in chunks
      for (k=(jLast>>CHECK_SHIFT)-1;k>=0;k--) {
	unsigned int iMark = mark[k];
	if (iMark) {
	  // something in chunk - do all (as imark may change)
	  int iLast = k<<CHECK_SHIFT;
	  i = iLast+BITS_PER_CHECK-1;
	  for ( ; i >= iLast; i-- ) {
	    double value = region[i];
	    double absValue = fabs ( value );
	    if ( value ) {
	      region[i]=0.0;
	      if ( absValue > tolerance ) {
		region[i]=-value;
		regionIndex[numberNonZero++]=i;
	      }
	    }
	  }
	  mark[k]=0;
	}
      }
    }
  }
  regionSparse->setNumElements ( numberNonZero );
  mark[(numberU_-1)>>CHECK_SHIFT]=0;
  mark[numberSlacks_>>CHECK_SHIFT]=0;
  if (numberSlacks_)
    mark[(numberSlacks_-1)>>CHECK_SHIFT]=0;
#ifdef COIN_DEBUG
  for (i=0;i<maximumRowsExtra_;i++) {
    assert (!mark[i]);
  }
#endif
}

//  =
CoinFactorization & CoinFactorization::operator = ( const CoinFactorization & other ) {
  if (this != &other) {    
    gutsOfDestructor();
    gutsOfInitialize(3);
    gutsOfCopy(other);
  }
  return *this;
}
void CoinFactorization::gutsOfCopy(const CoinFactorization &other)
{
  elementU_ = new double [ other.lengthAreaU_ ];
  indexRowU_ = new int [ other.lengthAreaU_ ];
  indexColumnU_ = new int [ other.lengthAreaU_ ];
  convertRowToColumnU_ = new CoinBigIndex [ other.lengthAreaU_ ];
  if (other.sparseThreshold_) {
    elementByRowL_ = new double [ other.lengthAreaL_ ];
    indexColumnL_ = new int [ other.lengthAreaL_ ];
    startRowL_ = new CoinBigIndex [other.numberRows_+1];
  }
  elementL_ = new double [ other.lengthAreaL_ ];
  indexRowL_ = new int [ other.lengthAreaL_ ];
  startColumnL_ = new CoinBigIndex [ other.numberRows_ + 1 ];
  int extraSpace = other.maximumColumnsExtra_ - other.numberColumns_;

  startColumnR_ = new CoinBigIndex [ extraSpace + 1 ];
  startRowU_ = new CoinBigIndex [ other.maximumRowsExtra_ + 1 ];
  numberInRow_ = new int [ other.maximumRowsExtra_ + 1 ];
  nextRow_ = new int [ other.maximumRowsExtra_ + 1 ];
  lastRow_ = new int [ other.maximumRowsExtra_ + 1 ];
  pivotRegion_ = new double [ other.maximumRowsExtra_ + 1 ];
  permuteBack_ = new int [ other.maximumRowsExtra_ + 1 ];
  permute_ = new int [ other.maximumRowsExtra_ + 1 ];
  pivotColumnBack_ = new int [ other.maximumRowsExtra_ + 1 ];
  startColumnU_ = new CoinBigIndex [ other.maximumColumnsExtra_ + 1 ];
  numberInColumn_ = new int [ other.maximumColumnsExtra_ + 1 ];
  pivotColumn_ = new int [ other.maximumColumnsExtra_ + 1 ];
  nextColumn_ = new int [ other.maximumColumnsExtra_ + 1 ];
  lastColumn_ = new int [ other.maximumColumnsExtra_ + 1 ];
  numberTrials_ = other.numberTrials_;
  biggerDimension_ = other.biggerDimension_;
  relaxCheck_ = other.relaxCheck_;
  numberSlacks_ = other.numberSlacks_;
  numberU_ = other.numberU_;
  maximumU_=other.maximumU_;
  lengthU_ = other.lengthU_;
  lengthAreaU_ = other.lengthAreaU_;
  numberL_ = other.numberL_;
  baseL_ = other.baseL_;
  lengthL_ = other.lengthL_;
  lengthAreaL_ = other.lengthAreaL_;
  numberR_ = other.numberR_;
  lengthR_ = other.lengthR_;
  lengthAreaR_ = other.lengthAreaR_;
  pivotTolerance_ = other.pivotTolerance_;
  zeroTolerance_ = other.zeroTolerance_;
  slackValue_ = other.slackValue_;
  areaFactor_ = other.areaFactor_;
  numberRows_ = other.numberRows_;
  numberRowsExtra_ = other.numberRowsExtra_;
  maximumRowsExtra_ = other.maximumRowsExtra_;
  numberColumns_ = other.numberColumns_;
  numberColumnsExtra_ = other.numberColumnsExtra_;
  maximumColumnsExtra_ = other.maximumColumnsExtra_;
  maximumPivots_=other.maximumPivots_;
  numberGoodU_ = other.numberGoodU_;
  numberGoodL_ = other.numberGoodL_;
  numberPivots_ = other.numberPivots_;
  messageLevel_ = other.messageLevel_;
  totalElements_ = other.totalElements_;
  factorElements_ = other.factorElements_;
  status_ = other.status_;
  doForrestTomlin_ = other.doForrestTomlin_;
  collectStatistics_=other.collectStatistics_;
  ftranCountInput_=other.ftranCountInput_;
  ftranCountAfterL_=other.ftranCountAfterL_;
  ftranCountAfterR_=other.ftranCountAfterR_;
  ftranCountAfterU_=other.ftranCountAfterU_;
  btranCountInput_=other.btranCountInput_;
  btranCountAfterU_=other.btranCountAfterU_;
  btranCountAfterR_=other.btranCountAfterR_;
  btranCountAfterL_=other.btranCountAfterL_;
  numberFtranCounts_=other.numberFtranCounts_;
  numberBtranCounts_=other.numberBtranCounts_;
  ftranAverageAfterL_=other.ftranAverageAfterL_;
  ftranAverageAfterR_=other.ftranAverageAfterR_;
  ftranAverageAfterU_=other.ftranAverageAfterU_;
  btranAverageAfterU_=other.btranAverageAfterU_;
  btranAverageAfterR_=other.btranAverageAfterR_;
  btranAverageAfterL_=other.btranAverageAfterL_; 
  biasLU_=other.biasLU_;
  sparseThreshold_=other.sparseThreshold_;
  sparseThreshold2_=other.sparseThreshold2_;
  CoinBigIndex space = lengthAreaL_ - lengthL_;

  numberDense_ = other.numberDense_;
  denseThreshold_=other.denseThreshold_;
  if (numberDense_) {
    denseArea_ = new double [numberDense_*numberDense_];
    memcpy(denseArea_,other.denseArea_,
	   numberDense_*numberDense_*sizeof(double));
    densePermute_ = new int [numberDense_];
    memcpy(densePermute_,other.densePermute_,
	   numberDense_*sizeof(int));
  }

  lengthAreaR_ = space;
  elementR_ = elementL_ + lengthL_;
  indexRowR_ = indexRowL_ + lengthL_;
  //now copy everything
  //assuming numberRowsExtra==numberColumnsExtra
  if (numberRowsExtra_) {
    CoinMemcpyN ( other.startRowU_, numberRowsExtra_ + 1, startRowU_ );
    CoinMemcpyN ( other.numberInRow_, numberRowsExtra_ + 1, numberInRow_ );
    CoinMemcpyN ( other.nextRow_, numberRowsExtra_ + 1, nextRow_ );
    CoinMemcpyN ( other.lastRow_, numberRowsExtra_ + 1, lastRow_ );
    CoinMemcpyN ( other.pivotRegion_, numberRowsExtra_ + 1, pivotRegion_ );
    CoinMemcpyN ( other.permuteBack_, numberRowsExtra_ + 1, permuteBack_ );
    CoinMemcpyN ( other.permute_, numberRowsExtra_ + 1, permute_ );
    CoinMemcpyN ( other.pivotColumnBack_, numberRowsExtra_ + 1, pivotColumnBack_ );
    CoinMemcpyN ( other.startColumnU_, numberRowsExtra_ + 1, startColumnU_ );
    CoinMemcpyN ( other.numberInColumn_, numberRowsExtra_ + 1, numberInColumn_ );
    CoinMemcpyN ( other.pivotColumn_, numberRowsExtra_ + 1, pivotColumn_ );
    CoinMemcpyN ( other.nextColumn_, numberRowsExtra_ + 1, nextColumn_ );
    CoinMemcpyN ( other.lastColumn_, numberRowsExtra_ + 1, lastColumn_ );
    CoinMemcpyN ( other.startColumnR_ , numberRowsExtra_ - numberColumns_ + 1,
			startColumnR_ );  
    //extra one at end
    startColumnU_[maximumColumnsExtra_] =
      other.startColumnU_[maximumColumnsExtra_];
    nextColumn_[maximumColumnsExtra_] = other.nextColumn_[maximumColumnsExtra_];
    lastColumn_[maximumColumnsExtra_] = other.lastColumn_[maximumColumnsExtra_];
    startRowU_[maximumRowsExtra_] = other.startRowU_[maximumRowsExtra_];
    nextRow_[maximumRowsExtra_] = other.nextRow_[maximumRowsExtra_];
    lastRow_[maximumRowsExtra_] = other.lastRow_[maximumRowsExtra_];
  }
  CoinMemcpyN ( other.elementR_, lengthR_, elementR_ );
  CoinMemcpyN ( other.indexRowR_, lengthR_, indexRowR_ );
  //row and column copies of U
  /* as elements of U may have been zeroed but column counts zero
     copy all elements */
  int iRow;
#ifndef NDEBUG
  int maxU=0;
  for ( iRow = 0; iRow < numberRowsExtra_; iRow++ ) {
    CoinBigIndex start = startColumnU_[iRow];
    int numberIn = numberInColumn_[iRow];
    maxU = CoinMax(maxU,start+numberIn);
  }
  assert (maximumU_>=maxU);
#endif
  CoinMemcpyN ( other.elementU_ , maximumU_, elementU_ );
  for ( iRow = 0; iRow < numberRowsExtra_; iRow++ ) {
    //row
    CoinBigIndex start = startRowU_[iRow];
    int numberIn = numberInRow_[iRow];

    CoinMemcpyN ( other.indexColumnU_ + start, numberIn, indexColumnU_ + start );
    CoinMemcpyN (other.convertRowToColumnU_ + start , numberIn,
	     convertRowToColumnU_ + start );
    //column
    start = startColumnU_[iRow];
    numberIn = numberInColumn_[iRow];
    CoinMemcpyN ( other.indexRowU_ + start, numberIn, indexRowU_ + start );
  }
  // L is contiguous
  if (numberRows_)
    CoinMemcpyN ( other.startColumnL_, numberRows_ + 1, startColumnL_ );
  CoinMemcpyN ( other.elementL_, lengthL_, elementL_ );
  CoinMemcpyN ( other.indexRowL_, lengthL_, indexRowL_ );
  if (other.sparseThreshold_) {
    goSparse();
  }
}
//  updateColumnR.  Updates part of column (FTRANR)
void
CoinFactorization::updateColumnR ( CoinIndexedVector * regionSparse ) const
{
  double *region = regionSparse->denseVector (  );
  int *regionIndex = regionSparse->getIndices (  );
  int numberNonZero = regionSparse->getNumElements (  );

  if ( !numberR_ )
    return;	//return if nothing to do
  double tolerance = zeroTolerance_;

  const CoinBigIndex * startColumn = startColumnR_-numberRows_;
  const int * indexRow = indexRowR_;
  const double * element = elementR_;
  const int * permute = permute_;

  int iRow;
  double pivotValue;
  int i;

  // Work out very dubious idea of what would be fastest
  int method=-1;
  // Size of R
  double sizeR=startColumnR_[numberR_];
  // Average
  double averageR = sizeR/((double) numberRowsExtra_);
  // weights (relative to actual work)
  double setMark = 0.1; // setting mark
  double test1= 1.0; // starting ftran (without testPivot)
  double testPivot = 2.0; // Seeing if zero etc
  double startDot=2.0; // For starting dot product version
  // For final scan
  double final = numberNonZero*1.0;
  double methodTime[3];
  // For second type
  methodTime[1] = numberPivots_ * (testPivot + (((double) numberNonZero)/((double) numberRows_)
						* averageR));
  methodTime[1] += numberNonZero *(test1 + averageR);
  // For first type
  methodTime[0] = methodTime[1] + (numberNonZero+numberPivots_)*setMark;
  methodTime[1] += numberNonZero*final;
  // third
  methodTime[2] = sizeR + numberPivots_*startDot + numberNonZero*final;
  // switch off if necessary
  if (!numberInColumnPlus_) {
    methodTime[0]=1.0e100;
    methodTime[1]=1.0e100;
  } else if (!sparse_) {
    methodTime[0]=1.0e100;
  }
  double best=1.0e100;
  for (i=0;i<3;i++) {
    if (methodTime[i]<best) {
      best=methodTime[i];
      method=i;
    }
  }
  assert (method>=0);
  //if (method==1)
  //printf(" methods %g %g %g - chosen %d\n",methodTime[0],methodTime[1],methodTime[2],method);

  switch (method) {
  case 0:
#ifdef STACK
    {
      // use sparse_ as temporary area
      // mark known to be zero
      int * stack = sparse_;  /* pivot */
      int * list = stack + maximumRowsExtra_;  /* final list */
      CoinBigIndex * next = (CoinBigIndex *) (list + maximumRowsExtra_);  /* jnext */
      char * mark = (char *) (next + maximumRowsExtra_);
      // we have another copy of R in R
      double * elementR = elementR_ + lengthAreaR_;
      int * indexRowR = indexRowR_ + lengthAreaR_;
      CoinBigIndex * startR = startColumnR_+maximumPivots_+1;
      int k,nStack;
      int nList=0;
      for (k=0;k<numberNonZero;k++) {
	int kPivot=regionIndex[k];
	if(!mark[kPivot]) {
	  stack[0]=kPivot;
	  CoinBigIndex j=-10;
	  next[0]=j;
	  nStack=0;
	  while (nStack>=0) {
	    /* take off stack */
	    if (j>=startR[kPivot]) {
	      int jPivot=indexRowR[j--];
	      /* put back on stack */
	      next[nStack] =j;
	      if (!mark[jPivot]) {
		/* and new one */
		kPivot=jPivot;
		j=-10;
		stack[++nStack]=kPivot;
		mark[kPivot]=1;
		next[nStack]=j;
	      }
	    } else if (j==-10) {
	      // before first - see if followon
	      int jPivot = permuteBack_[kPivot];
	      if (jPivot<numberRows_) {
		// no
		j=startR[kPivot]+numberInColumnPlus_[kPivot]-1;
		next[nStack]=j;
	      } else {
		// add to list
		if (!mark[jPivot]) {
		  /* and new one */
		  kPivot=jPivot;
		  j=-10;
		  stack[++nStack]=kPivot;
		  mark[kPivot]=1;
		  next[nStack]=j;
		} else {
		  j=startR[kPivot]+numberInColumnPlus_[kPivot]-1;
		  next[nStack]=j;
		}
	      }
	    } else {
	      // finished
	      list[nList++]=kPivot;
	      mark[kPivot]=1;
	      --nStack;
	      if (nStack>=0) {
		kPivot=stack[nStack];
		j=next[nStack];
	      }
	    }
	  }
	}
      }
      numberNonZero=0;
      for (i=nList-1;i>=0;i--) {
	// for now sort
	//std::sort(list,list+nList);
	//for (i=0;i<nList;i++) {
	int iPivot = list[i];
	mark[iPivot]=0;
	if (iPivot<numberRows_) {
	  pivotValue = region[iPivot];
	} else {
	  int before = permute[iPivot];
	  pivotValue = region[iPivot] + region[before];
	  region[before]=0.0;
	}
	if ( fabs ( pivotValue ) > tolerance ) {
	  region[iPivot] = pivotValue;
	  CoinBigIndex start = startR[iPivot];
	  int number = numberInColumnPlus_[iPivot];
	  CoinBigIndex end = start + number;
	  CoinBigIndex j;
	  for (j=start ; j < end; j ++ ) {
	    int iRow = indexRowR[j];
	    double value = elementR[j];
	    region[iRow] -= value * pivotValue;
	  }     
	  regionIndex[numberNonZero++] = iPivot;
	} else {
	  region[iPivot] = 0.0;
	}       
      }
    }
#else
    {
      
      // use sparse_ as temporary area
      // mark known to be zero
      int * stack = sparse_;  /* pivot */
      int * list = stack + maximumRowsExtra_;  /* final list */
      CoinBigIndex * next = (CoinBigIndex *) (list + maximumRowsExtra_);  /* jnext */
      char * mark = (char *) (next + maximumRowsExtra_);
      // mark all rows which will be permuted
      for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	iRow = permute[i];
	mark[iRow]=1;
      }
      // we have another copy of R in R
      double * elementR = elementR_ + lengthAreaR_;
      int * indexRowR = indexRowR_ + lengthAreaR_;
      CoinBigIndex * startR = startColumnR_+maximumPivots_+1;
      // For current list order does not matter as
      // only affects end
      int newNumber=0;
      for ( i = 0; i < numberNonZero; i++ ) {
	int iRow = regionIndex[i];
	assert (region[iRow]);
	if (!mark[iRow])
	  regionIndex[newNumber++]=iRow;
	int number = numberInColumnPlus_[iRow];
	if (number) {
	  pivotValue = region[iRow];
	  CoinBigIndex j;
	  CoinBigIndex start=startR[iRow];
	  CoinBigIndex end = start+number;
	  for ( j = start; j < end; j ++ ) {
	    double value = elementR[j];
	    int jRow = indexRowR[j];
	    region[jRow] -= pivotValue*value;
	  }
	}
      }
      numberNonZero = newNumber;
      for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	//move using permute_ (stored in inverse fashion)
	iRow = permute[i];
	pivotValue = region[iRow]+region[i];
	//zero out pre-permuted
	region[iRow] = 0.0;
	if ( fabs ( pivotValue ) > tolerance ) {
	  region[i] = pivotValue;
	  if (!mark[i])
	    regionIndex[numberNonZero++] = i;
	  int number = numberInColumnPlus_[i];
	  CoinBigIndex j;
	  CoinBigIndex start=startR[i];
	  CoinBigIndex end = start+number;
	  for ( j = start; j < end; j ++ ) {
	    double value = elementR[j];
	    int jRow = indexRowR[j];
	    region[jRow] -= pivotValue*value;
	  }
	} else {
	  region[i] = 0.0;
	}
	mark[iRow]=0;
      }
    }
#endif
    break;
  case 1:
    {
      // no sparse region
      // we have another copy of R in R
      double * elementR = elementR_ + lengthAreaR_;
      int * indexRowR = indexRowR_ + lengthAreaR_;
      CoinBigIndex * startR = startColumnR_+maximumPivots_+1;
      // For current list order does not matter as
      // only affects end
      for ( i = 0; i < numberNonZero; i++ ) {
	int iRow = regionIndex[i];
	assert (region[iRow]);
	int number = numberInColumnPlus_[iRow];
	if (number) {
	  pivotValue = region[iRow];
	  CoinBigIndex j;
	  CoinBigIndex start=startR[iRow];
	  CoinBigIndex end = start+number;
	  for ( j = start; j < end; j ++ ) {
	    double value = elementR[j];
	    int jRow = indexRowR[j];
	    region[jRow] -= pivotValue*value;
	  }
	}
      }
      for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	//move using permute_ (stored in inverse fashion)
	iRow = permute[i];
	pivotValue = region[iRow]+region[i];
	//zero out pre-permuted
	region[iRow] = 0.0;
	if ( fabs ( pivotValue ) > tolerance ) {
	  region[i] = pivotValue;
	  regionIndex[numberNonZero++] = i;
	  int number = numberInColumnPlus_[i];
	  CoinBigIndex j;
	  CoinBigIndex start=startR[i];
	  CoinBigIndex end = start+number;
	  for ( j = start; j < end; j ++ ) {
	    double value = elementR[j];
	    int jRow = indexRowR[j];
	    region[jRow] -= pivotValue*value;
	  }
	} else {
	  region[i] = 0.0;
	}
      }
    }
    break;
  case 2:
    {
      CoinBigIndex start = startColumn[numberRows_];
      for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	//move using permute_ (stored in inverse fashion)
	CoinBigIndex end = startColumn[i+1];
	iRow = permute[i];
	pivotValue = region[iRow];
	//zero out pre-permuted
	region[iRow] = 0.0;
	
	CoinBigIndex j;
	for ( j = start; j < end; j ++ ) {
	  double value = element[j];
	  int jRow = indexRow[j];
	  value *= region[jRow];
	  pivotValue -= value;
	}
	start=end;
	if ( fabs ( pivotValue ) > tolerance ) {
	  region[i] = pivotValue;
	  regionIndex[numberNonZero++] = i;
	} else {
	region[i] = 0.0;
	}
      }
    }
    break;
  }
  if (method) {
    // pack down
    int i,n=numberNonZero;
    numberNonZero=0;
    for (i=0;i<n;i++) {
      int indexValue = regionIndex[i];
      double value = region[indexValue];
      if (value) 
	regionIndex[numberNonZero++]=indexValue;
    }
  }
  //set counts
  regionSparse->setNumElements ( numberNonZero );
}
//  updateColumnR.  Updates part of column (FTRANR)
void
CoinFactorization::updateColumnRFT ( CoinIndexedVector * regionSparse,
				     int * regionIndex) 
{
  double *region = regionSparse->denseVector (  );
  //int *regionIndex = regionSparse->getIndices (  );
  int numberNonZero = regionSparse->getNumElements (  );

  if ( numberR_ ) {
    double tolerance = zeroTolerance_;
    
    const CoinBigIndex * startColumn = startColumnR_-numberRows_;
    const int * indexRow = indexRowR_;
    const double * element = elementR_;
    const int * permute = permute_;
    
    int iRow;
    double pivotValue;
    
    int i;
    // Work out very dubious idea of what would be fastest
    int method=-1;
    // Size of R
    double sizeR=startColumnR_[numberR_];
    // Average
    double averageR = sizeR/((double) numberRowsExtra_);
    // weights (relative to actual work)
    double setMark = 0.1; // setting mark
    double test1= 1.0; // starting ftran (without testPivot)
    double testPivot = 2.0; // Seeing if zero etc
    double startDot=2.0; // For starting dot product version
    // For final scan
    double final = numberNonZero*1.0;
    double methodTime[3];
    // For second type
    methodTime[1] = numberPivots_ * (testPivot + (((double) numberNonZero)/((double) numberRows_)
						  * averageR));
    methodTime[1] += numberNonZero *(test1 + averageR);
    // For first type
    methodTime[0] = methodTime[1] + (numberNonZero+numberPivots_)*setMark;
    methodTime[1] += numberNonZero*final;
    // third
    methodTime[2] = sizeR + numberPivots_*startDot + numberNonZero*final;
    // switch off if necessary
    if (!numberInColumnPlus_) {
      methodTime[0]=1.0e100;
      methodTime[1]=1.0e100;
    } else if (!sparse_) {
      methodTime[0]=1.0e100;
    }
    // adjust for final scan
    methodTime[1] += final;
    double best=1.0e100;
    for (i=0;i<3;i++) {
      if (methodTime[i]<best) {
	best=methodTime[i];
	method=i;
      }
    }
    assert (method>=0);
    //if (method==1)
    //printf(" methods %g %g %g - chosen %d\n",methodTime[0],methodTime[1],methodTime[2],method);
    
    switch (method) {
    case 0:
      {
	// use sparse_ as temporary area
	// mark known to be zero
	int * stack = sparse_;  /* pivot */
	int * list = stack + maximumRowsExtra_;  /* final list */
	CoinBigIndex * next = (CoinBigIndex *) (list + maximumRowsExtra_);  /* jnext */
	char * mark = (char *) (next + maximumRowsExtra_);
	// mark all rows which will be permuted
	for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	  iRow = permute[i];
	  mark[iRow]=1;
	}
	// we have another copy of R in R
	double * elementR = elementR_ + lengthAreaR_;
	int * indexRowR = indexRowR_ + lengthAreaR_;
	CoinBigIndex * startR = startColumnR_+maximumPivots_+1;
	//save in U
	//in at end
	int iColumn = numberColumnsExtra_;

	startColumnU_[iColumn] = startColumnU_[maximumColumnsExtra_];
	CoinBigIndex start = startColumnU_[iColumn];
  
	//int * putIndex = indexRowU_ + start;
	double * putElement = elementU_ + start;
	// For current list order does not matter as
	// only affects end
	int newNumber=0;
	for ( i = 0; i < numberNonZero; i++ ) {
	  int iRow = regionIndex[i];
	  pivotValue = region[iRow];
	  assert (region[iRow]);
	  if (!mark[iRow]) {
	    //putIndex[newNumber]=iRow;
	    putElement[newNumber]=pivotValue;;
	    regionIndex[newNumber++]=iRow;
	  }
	  int number = numberInColumnPlus_[iRow];
	  if (number) {
	    CoinBigIndex j;
	    CoinBigIndex start=startR[iRow];
	    CoinBigIndex end = start+number;
	    for ( j = start; j < end; j ++ ) {
	      double value = elementR[j];
	      int jRow = indexRowR[j];
	      region[jRow] -= pivotValue*value;
	    }
	  }
	}
	numberNonZero = newNumber;
	for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	  //move using permute_ (stored in inverse fashion)
	  iRow = permute[i];
	  pivotValue = region[iRow]+region[i];
	  //zero out pre-permuted
	  region[iRow] = 0.0;
	  if ( fabs ( pivotValue ) > tolerance ) {
	    region[i] = pivotValue;
	    if (!mark[i]) {
	      //putIndex[numberNonZero]=i;
	      putElement[numberNonZero]=pivotValue;;
	      regionIndex[numberNonZero++]=i;
	    }
	    int number = numberInColumnPlus_[i];
	    CoinBigIndex j;
	    CoinBigIndex start=startR[i];
	    CoinBigIndex end = start+number;
	    for ( j = start; j < end; j ++ ) {
	      double value = elementR[j];
	      int jRow = indexRowR[j];
	      region[jRow] -= pivotValue*value;
	    }
	  } else {
	    region[i] = 0.0;
	  }
	  mark[iRow]=0;
	}
	numberInColumn_[iColumn] = numberNonZero;
	startColumnU_[maximumColumnsExtra_] = start + numberNonZero;
      }
      break;
    case 1:
      {
	// no sparse region
	// we have another copy of R in R
	double * elementR = elementR_ + lengthAreaR_;
	int * indexRowR = indexRowR_ + lengthAreaR_;
	CoinBigIndex * startR = startColumnR_+maximumPivots_+1;
	// For current list order does not matter as
	// only affects end
	for ( i = 0; i < numberNonZero; i++ ) {
	  int iRow = regionIndex[i];
	  assert (region[iRow]);
	  int number = numberInColumnPlus_[iRow];
	  if (number) {
	    pivotValue = region[iRow];
	    CoinBigIndex j;
	    CoinBigIndex start=startR[iRow];
	    CoinBigIndex end = start+number;
	    for ( j = start; j < end; j ++ ) {
	      double value = elementR[j];
	      int jRow = indexRowR[j];
	      region[jRow] -= pivotValue*value;
	    }
	  }
	}
	for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	  //move using permute_ (stored in inverse fashion)
	  iRow = permute[i];
	  pivotValue = region[iRow]+region[i];
	  //zero out pre-permuted
	  region[iRow] = 0.0;
	  if ( fabs ( pivotValue ) > tolerance ) {
	    region[i] = pivotValue;
	    regionIndex[numberNonZero++] = i;
	    int number = numberInColumnPlus_[i];
	    CoinBigIndex j;
	    CoinBigIndex start=startR[i];
	    CoinBigIndex end = start+number;
	    for ( j = start; j < end; j ++ ) {
	      double value = elementR[j];
	      int jRow = indexRowR[j];
	      region[jRow] -= pivotValue*value;
	    }
	  } else {
	    region[i] = 0.0;
	  }
	}
      }
      break;
    case 2:
      {
	CoinBigIndex start = startColumn[numberRows_];
	for ( i = numberRows_; i < numberRowsExtra_; i++ ) {
	  //move using permute_ (stored in inverse fashion)
	  CoinBigIndex end = startColumn[i+1];
	  iRow = permute[i];
	  pivotValue = region[iRow];
	  //zero out pre-permuted
	  region[iRow] = 0.0;
	  
	  CoinBigIndex j;
	  for ( j = start; j < end; j ++ ) {
	    double value = element[j];
	    int jRow = indexRow[j];
	    value *= region[jRow];
	    pivotValue -= value;
	  }
	  start=end;
	  if ( fabs ( pivotValue ) > tolerance ) {
	    region[i] = pivotValue;
	    regionIndex[numberNonZero++] = i;
	  } else {
	    region[i] = 0.0;
	  }
	}
      }
      break;
    }
    if (method) {
      // pack down
      int i,n=numberNonZero;
      numberNonZero=0;
      //save in U
      //in at end
      int iColumn = numberColumnsExtra_;
      
      startColumnU_[iColumn] = startColumnU_[maximumColumnsExtra_];
      CoinBigIndex start = startColumnU_[iColumn];
      
      int * putIndex = indexRowU_ + start;
      double * putElement = elementU_ + start;
      for (i=0;i<n;i++) {
	int indexValue = regionIndex[i];
	double value = region[indexValue];
	if (value) {
	  putIndex[numberNonZero]=indexValue;
	  putElement[numberNonZero]=value;
	  regionIndex[numberNonZero++]=indexValue;
	}
      }
      numberInColumn_[iColumn] = numberNonZero;
      startColumnU_[maximumColumnsExtra_] = start + numberNonZero;
    }
    //set counts
    regionSparse->setNumElements ( numberNonZero );
  } else {
    // No R but we still need to save column
    //save in U
    //in at end
    numberNonZero = regionSparse->getNumElements (  );
    int iColumn = numberColumnsExtra_;
    
    startColumnU_[iColumn] = startColumnU_[maximumColumnsExtra_];
    CoinBigIndex start = startColumnU_[iColumn];
    numberInColumn_[iColumn] = numberNonZero;
    startColumnU_[maximumColumnsExtra_] = start + numberNonZero;
    
    int * putIndex = indexRowU_ + start;
    double * putElement = elementU_ + start;
    int i;
    for (i=0;i<numberNonZero;i++) {
      int indexValue = regionIndex[i];
      double value = region[indexValue];
      putIndex[i]=indexValue;
      putElement[i]=value;
    }
  }
}
// Updates part of column transpose (BTRANR) when dense
void 
CoinFactorization::updateColumnTransposeRDensish 
( CoinIndexedVector * regionSparse ) const
{
  double *region = regionSparse->denseVector (  );
  int i;

#ifdef COIN_DEBUG
  regionSparse->checkClean();
#endif
  int last = numberRowsExtra_-1;
  
  
  const int *indexRow = indexRowR_;
  const double *element = elementR_;
  const CoinBigIndex * startColumn = startColumnR_-numberRows_;
  //move using permute_ (stored in inverse fashion)
  const int * permute = permute_;
  int putRow;
  double pivotValue;
  
  for ( i = last ; i >= numberRows_; i-- ) {
    putRow = permute[i];
    pivotValue = region[i];
    //zero out  old permuted
    region[i] = 0.0;
    if ( pivotValue ) {
      CoinBigIndex j;
      for ( j = startColumn[i]; j < startColumn[i+1]; j++ ) {
	double value = element[j];
	int iRow = indexRow[j];
	region[iRow] -= value * pivotValue;
      }
      region[putRow] = pivotValue;
      //putRow must have been zero before so put on list ??
      //but can't catch up so will have to do L from end
      //unless we update lookBtran in replaceColumn - yes
    }
  }
}
// Updates part of column transpose (BTRANR) when sparse
void 
CoinFactorization::updateColumnTransposeRSparse 
( CoinIndexedVector * regionSparse ) const
{
  double *region = regionSparse->denseVector (  );
  int *regionIndex = regionSparse->getIndices (  );
  int numberNonZero = regionSparse->getNumElements (  );
  double tolerance = zeroTolerance_;
  int i;

#ifdef COIN_DEBUG
  regionSparse->checkClean();
#endif
  int last = numberRowsExtra_-1;
  
  
  const int *indexRow = indexRowR_;
  const double *element = elementR_;
  const CoinBigIndex * startColumn = startColumnR_-numberRows_;
  //move using permute_ (stored in inverse fashion)
  const int * permute = permute_;
  int putRow;
  double pivotValue;
    
  // we can use sparse_ as temporary array
  int * spare = sparse_;
  for (i=0;i<numberNonZero;i++) {
    spare[regionIndex[i]]=i;
  }
  // still need to do in correct order (for now)
  for ( i = last ; i >= numberRows_; i-- ) {
    putRow = permute[i];
    assert (putRow<=i);
    pivotValue = region[i];
    //zero out  old permuted
    region[i] = 0.0;
    if ( pivotValue ) {
      CoinBigIndex j;
      for ( j = startColumn[i]; j < startColumn[i+1]; j++ ) {
	double value = element[j];
	int iRow = indexRow[j];
	double oldValue = region[iRow];
	double newValue = oldValue - value * pivotValue;
	if (oldValue) {
	  if (!newValue)
	    newValue=COIN_INDEXED_REALLY_TINY_ELEMENT;
	  region[iRow]=newValue;
	} else if (fabs(newValue)>tolerance) {
	  region[iRow] = newValue;
	  spare[iRow]=numberNonZero;
	  regionIndex[numberNonZero++]=iRow;
	}
      }
      region[putRow] = pivotValue;
      // modify list
      int position=spare[i];
      regionIndex[position]=putRow;
      spare[putRow]=position;
    }
  }
  regionSparse->setNumElements(numberNonZero);
}

//  updateColumnTransposeR.  Updates part of column (FTRANR)
void
CoinFactorization::updateColumnTransposeR ( CoinIndexedVector * regionSparse ) const
{
  if (numberRowsExtra_==numberRows_)
    return;
  int numberNonZero = regionSparse->getNumElements (  );

  if (numberNonZero) {
    if (numberNonZero < (sparseThreshold_<<2)||(!numberL_&&sparse_)) {
      updateColumnTransposeRSparse ( regionSparse );
      if (collectStatistics_) 
	btranCountAfterR_ += (double) regionSparse->getNumElements();
    } else {
      updateColumnTransposeRDensish ( regionSparse );
      // we have lost indices
      // make sure won't try and go sparse again
      if (collectStatistics_) 
	btranCountAfterR_ += (double) CoinMin((numberNonZero<<1),numberRows_);
      regionSparse->setNumElements (numberRows_+1);
    }
  }
}
/* Updates one column (FTRAN) from region2 and permutes.
   region1 starts as zero
   Note - if regionSparse2 packed on input - will be packed on output
   - returns un-permuted result in region2 and region1 is zero */
int CoinFactorization::updateColumnFT ( CoinIndexedVector * regionSparse,
					CoinIndexedVector * regionSparse2)
{
  //permute and move indices into index array
  int *regionIndex = regionSparse->getIndices (  );
  int numberNonZero = regionSparse2->getNumElements();
  const int *permute = permute_;
  int * index = regionSparse2->getIndices();
  double * region = regionSparse->denseVector();
  double * array = regionSparse2->denseVector();
  bool doFT=doForrestTomlin_;
  // see if room
  if (doFT) {
    int iColumn = numberColumnsExtra_;
    
    startColumnU_[iColumn] = startColumnU_[maximumColumnsExtra_];
    CoinBigIndex start = startColumnU_[iColumn];
    CoinBigIndex space = lengthAreaU_ - ( start + numberRows_ );
    doFT = space>=0;
    if (doFT) {
      regionIndex = indexRowU_ + start;
    } else {
      startColumnU_[maximumColumnsExtra_] = lengthAreaU_+1;
    }
  }

  int j;
  bool packed = regionSparse2->packedMode();
  if (packed) {
    for ( j = 0; j < numberNonZero; j ++ ) {
      int iRow = index[j];
      double value = array[j];
      array[j]=0.0;
      iRow = permute[iRow];
      region[iRow] = value;
      regionIndex[j] = iRow;
    }
  } else {
    for ( j = 0; j < numberNonZero; j ++ ) {
      int iRow = index[j];
      double value = array[iRow];
      array[iRow]=0.0;
      iRow = permute[iRow];
      region[iRow] = value;
      regionIndex[j] = iRow;
    }
  }
  regionSparse->setNumElements ( numberNonZero );
  if (collectStatistics_) {
    numberFtranCounts_++;
    ftranCountInput_ += (double) numberNonZero;
  }
    
  //  ******* L
  updateColumnL ( regionSparse, regionIndex );
  if (collectStatistics_) 
    ftranCountAfterL_ += (double) regionSparse->getNumElements();
  //permute extra
  //row bits here
  if ( doFT ) 
    updateColumnRFT ( regionSparse, regionIndex );
  else
    updateColumnR ( regionSparse );
  if (collectStatistics_) 
    ftranCountAfterR_ += (double) regionSparse->getNumElements();
  //  ******* U
  updateColumnU ( regionSparse, regionIndex);
  if (!doForrestTomlin_) {
    // Do PFI after everything else
    updateColumnPFI(regionSparse);
  }
  numberNonZero = regionSparse->getNumElements (  );
  permuteBack(regionSparse,regionSparse2);
  // will be negative if no room
  if ( doFT ) 
    return numberNonZero;
  else 
    return -numberNonZero;
}
/* Updates one column (FTRAN) from region2 and permutes.
   region1 starts as zero
   Note - if regionSparse2 packed on input - will be packed on output
   - returns un-permuted result in region2 and region1 is zero */
int CoinFactorization::updateColumn ( CoinIndexedVector * regionSparse,
				      CoinIndexedVector * regionSparse2,
				      bool noPermute) 
  const
{
  //permute and move indices into index array
  int *regionIndex = regionSparse->getIndices (  );
  int numberNonZero;
  const int *permute = permute_;
  double * region = regionSparse->denseVector();

  if (!noPermute) {
    numberNonZero = regionSparse2->getNumElements();
    int * index = regionSparse2->getIndices();
    double * array = regionSparse2->denseVector();
    int j;
    bool packed = regionSparse2->packedMode();
    if (packed) {
      for ( j = 0; j < numberNonZero; j ++ ) {
	int iRow = index[j];
	double value = array[j];
	array[j]=0.0;
	iRow = permute[iRow];
	region[iRow] = value;
	regionIndex[j] = iRow;
      }
    } else {
      for ( j = 0; j < numberNonZero; j ++ ) {
	int iRow = index[j];
	double value = array[iRow];
	array[iRow]=0.0;
	iRow = permute[iRow];
	region[iRow] = value;
	regionIndex[j] = iRow;
      }
    }
    regionSparse->setNumElements ( numberNonZero );
  } else {
    numberNonZero = regionSparse->getNumElements();
  }
  if (collectStatistics_) {
    numberFtranCounts_++;
    ftranCountInput_ += (double) numberNonZero;
  }
    
  //  ******* L
  updateColumnL ( regionSparse, regionIndex );
  if (collectStatistics_) 
    ftranCountAfterL_ += (double) regionSparse->getNumElements();
  //permute extra
  //row bits here
  updateColumnR ( regionSparse );
  if (collectStatistics_) 
    ftranCountAfterR_ += (double) regionSparse->getNumElements();
  
  //update counts
  //  ******* U
  updateColumnU ( regionSparse, regionIndex);
  if (!doForrestTomlin_) {
    // Do PFI after everything else
    updateColumnPFI(regionSparse);
  }
  if (!noPermute) {
    permuteBack(regionSparse,regionSparse2);
    return regionSparse2->getNumElements (  );
  } else {
    return regionSparse->getNumElements (  );
  }
}
// Permutes back at end of updateColumn
void 
CoinFactorization::permuteBack ( CoinIndexedVector * regionSparse, 
				 CoinIndexedVector * outVector) const
{
  // permute back
  int oldNumber = regionSparse->getNumElements();
  int *regionIndex = regionSparse->getIndices (  );
  double * region = regionSparse->denseVector();
  int *outIndex = outVector->getIndices (  );
  double * out = outVector->denseVector();
  int * permuteBack = pivotColumnBack_;
  int j;
  int number=0;
  if (outVector->packedMode()) {
    for ( j = 0; j < oldNumber; j ++ ) {
      int iRow = regionIndex[j];
      double value = region[iRow];
      region[iRow]=0.0;
      if (fabs(value)>zeroTolerance_) {
	iRow = permuteBack[iRow];
	outIndex[number]=iRow;
	out[number++] = value;
      }
    }
  } else {
    for ( j = 0; j < oldNumber; j ++ ) {
      int iRow = regionIndex[j];
      double value = region[iRow];
      region[iRow]=0.0;
      if (fabs(value)>zeroTolerance_) {
	iRow = permuteBack[iRow];
	outIndex[number++]=iRow;
	out[iRow] = value;
      }
    }
  }
  outVector->setNumElements(number);
  regionSparse->setNumElements(0);
}
//  makes a row copy of L
void
CoinFactorization::goSparse ( )
{
  if (!sparseThreshold_) {
    if (numberRows_>300) {
      if (numberRows_<10000) {
	sparseThreshold_=CoinMin(numberRows_/6,500);
	sparseThreshold2_=sparseThreshold_;
      } else {
	sparseThreshold_=CoinMin(numberRows_/8,1000);
	sparseThreshold2_=sparseThreshold_+CoinMin((numberRows_-200)/5,2000);
	//sparseThreshold2_=sparseThreshold_;
      }
      sparseThreshold2_=numberRows_>>2;
    } else {
      sparseThreshold_=0;
      sparseThreshold2_=0;
    }
  } else {
    if (!sparseThreshold_&&numberRows_>400) {
      sparseThreshold_=CoinMin((numberRows_-300)/9,1000);
    }
    sparseThreshold2_=sparseThreshold_;
  }
  if (!sparseThreshold_)
    return;
  // allow for stack, list, next and char map of mark
  int nRowIndex = (maximumRowsExtra_+sizeof(int)-1)/
    sizeof(char);
  delete []sparse_;
  int nInBig = sizeof(CoinBigIndex)/sizeof(int);
  assert (nInBig>=1);
  sparse_ = new int [ (2+nInBig)*maximumRowsExtra_ + nRowIndex ];
  // zero out mark
  memset(sparse_+(2+nInBig)*maximumRowsExtra_,0,maximumRowsExtra_*sizeof(char));
  delete []elementByRowL_;
  delete []startRowL_;
  delete []indexColumnL_;
  startRowL_=new CoinBigIndex[numberRows_+1];
  if (lengthAreaL_) {
    elementByRowL_=new double[lengthAreaL_];
    indexColumnL_=new int[lengthAreaL_];
  } else {
    elementByRowL_=NULL;
    indexColumnL_=NULL;
  }
  // counts
  memset(startRowL_,0,numberRows_*sizeof(CoinBigIndex));
  int i;
  for (i=baseL_;i<baseL_+numberL_;i++) {
    CoinBigIndex j;
    for (j=startColumnL_[i];j<startColumnL_[i+1];j++) {
      int iRow = indexRowL_[j];
      startRowL_[iRow]++;
    }
  }
  // convert count to lasts
  CoinBigIndex count=0;
  for (i=0;i<numberRows_;i++) {
    int numberInRow=startRowL_[i];
    count += numberInRow;
    startRowL_[i]=count;
  }
  startRowL_[numberRows_]=count;
  // now insert
  for (i=baseL_+numberL_-1;i>=baseL_;i--) {
    CoinBigIndex j;
    for (j=startColumnL_[i];j<startColumnL_[i+1];j++) {
      int iRow = indexRowL_[j];
      CoinBigIndex start = startRowL_[iRow]-1;
      startRowL_[iRow]=start;
      elementByRowL_[start]=elementL_[j];
      indexColumnL_[start]=i;
    }
  }
}
//  get sparse threshold
int
CoinFactorization::sparseThreshold ( ) const
{
  return sparseThreshold_;
}

//  set sparse threshold
void
CoinFactorization::sparseThreshold ( int value ) 
{
  if (value>0&&sparseThreshold_) {
    sparseThreshold_=value;
    sparseThreshold2_= sparseThreshold_;
  } else if (!value&&sparseThreshold_) {
    // delete copy
    sparseThreshold_=0;
    sparseThreshold2_= 0;
    delete []elementByRowL_;
    delete []startRowL_;
    delete []indexColumnL_;
    elementByRowL_=NULL;
    startRowL_=NULL;
    indexColumnL_=NULL;
    delete []sparse_;
    sparse_=NULL;
  } else if (value>0&&!sparseThreshold_) {
    if (value>1)
      sparseThreshold_=value;
    else
      sparseThreshold_=0;
    sparseThreshold2_= sparseThreshold_;
    goSparse();
  }
}
void CoinFactorization::maximumPivots (  int value )
{
  if (value>0) {
    maximumPivots_=value;
  }
}
void CoinFactorization::messageLevel (  int value )
{
  if (value>0&&value<16) {
    messageLevel_=value;
  }
}
void CoinFactorization::pivotTolerance (  double value )
{
  if (value>0.0&&value<=1.0) {
    pivotTolerance_=value;
  }
}
void CoinFactorization::zeroTolerance (  double value )
{
  if (value>0.0&&value<1.0) {
    zeroTolerance_=value;
  }
}
void CoinFactorization::slackValue (  double value )
{
  if (value>=0.0) {
    slackValue_=1.0;
  } else {
    slackValue_=-1.0;
  }
}
// Reset all sparsity etc statistics
void CoinFactorization::resetStatistics()
{
  collectStatistics_=false;

  /// Below are all to collect
  ftranCountInput_=0.0;
  ftranCountAfterL_=0.0;
  ftranCountAfterR_=0.0;
  ftranCountAfterU_=0.0;
  btranCountInput_=0.0;
  btranCountAfterU_=0.0;
  btranCountAfterR_=0.0;
  btranCountAfterL_=0.0;

  /// We can roll over factorizations
  numberFtranCounts_=0;
  numberBtranCounts_=0;

  /// While these are averages collected over last 
  ftranAverageAfterL_=0.0;
  ftranAverageAfterR_=0.0;
  ftranAverageAfterU_=0.0;
  btranAverageAfterU_=0.0;
  btranAverageAfterR_=0.0;
  btranAverageAfterL_=0.0; 
}
/* Replaces one Row in basis,
   At present assumes just a singleton on row is in basis
   returns 0=OK, 1=Probably OK, 2=singular, 3 no space */
int 
CoinFactorization::replaceRow ( int whichRow, int numberInRow,
				const int indicesColumn[], const double elements[] )
{
  if (!numberInRow)
    return 0;
  int next = nextRow_[whichRow];
  int numberNow = numberInRow_[whichRow];
  CoinBigIndex start = startRowU_[whichRow];
  if (numberNow&&numberNow<100) {
    int ind[100];
    memcpy(ind,indexColumnU_+start,numberNow*sizeof(int));
    int i;
    for (i=0;i<numberInRow;i++) {
      int jColumn=indicesColumn[i];
      int k;
      for (k=0;k<numberNow;k++) {
	if (ind[k]==jColumn) {
	  ind[k]=-1;
	  break;
	}
      }
      if (k==numberNow) {
	printf("new column %d not in current\n",jColumn);
      } else {
	k=convertRowToColumnU_[start+k];
	double oldValue = elementU_[k];
	double newValue = elements[i]*pivotRegion_[jColumn];
	if (fabs(oldValue-newValue)>1.0e-3)
	  printf("column %d, old value %g new %g (el %g, piv %g)\n",jColumn,oldValue,
		 newValue,elements[i],pivotRegion_[jColumn]);
      }
    }
    for (i=0;i<numberNow;i++) {
      if (ind[i]>=0)
	printf("current column %d not in new\n",ind[i]);
    }
    assert (numberNow==numberInRow);
  }
  assert (numberInColumn_[whichRow]==0);
  assert (pivotRegion_[whichRow]==1.0);
  CoinBigIndex space;
  int returnCode=0;
      
  space = startRowU_[next] - (start+numberInRow);
  if ( space < 0 ) {
    if (!getRowSpaceIterate ( whichRow, numberInRow)) 
      returnCode=3;
  }
  //return 0;
  if (!returnCode) {
    numberInRow_[whichRow]=numberInRow;
    start = startRowU_[whichRow];
    int i;
    for (i=0;i<numberInRow;i++) {
      int iColumn = indicesColumn[i];
      indexColumnU_[start+i]=iColumn;
      assert (iColumn>whichRow);
      double value  = elements[i]*pivotRegion_[iColumn];
#if 0
      int k;
      bool found=false;
      for (k=startColumnU_[iColumn];k<startColumnU_[iColumn]+numberInColumn_[iColumn];k++) {
	if (indexRowU_[k]==whichRow) {
	  assert (fabs(elementU_[k]-value)<1.0e-3);
	  found=true;
	  break;
	}
      }
#if 0
      assert (found);
#else
      if (found) {
	int number = numberInColumn_[iColumn]-1;
	numberInColumn_[iColumn]=number;
	CoinBigIndex j=startColumnU_[iColumn]+number;
	if (k<j) {
	  int iRow=indexRowU_[j];
	  indexRowU_[k]=iRow;
	  elementU_[k]=elementU_[j];
	  int n=numberInRow_[iRow];
	  CoinBigIndex start = startRowU_[iRow];
	  for (j=start;j<start+n;j++) {
	    if (indexColumnU_[j]==iColumn) {
	      convertRowToColumnU_[j]=k;
	      break;
	    }
	  }
	  assert (j<start+n);
	}
      }
      found=false;
#endif
      if (!found) {
#endif
	CoinBigIndex iWhere = getColumnSpaceIterate(iColumn,value,whichRow);
	if (iWhere>=0) {
	  convertRowToColumnU_[start+i] = iWhere;
	} else {
	  returnCode=3;
	  break;
	}
#if 0
      } else {
	convertRowToColumnU_[start+i] = k;
      }
#endif
    }
  }       
  return returnCode;
}
// Takes out all entries for given rows
void 
CoinFactorization::emptyRows(int numberToEmpty, const int which[])
{
  int i;
  int * delRow = new int [maximumRowsExtra_];
  for (i=0;i<maximumRowsExtra_;i++)
    delRow[i]=0;
  for (i=0;i<numberToEmpty;i++) {
    int iRow = which[i];
    delRow[iRow]=1;
    assert (numberInColumn_[iRow]==0);
    assert (pivotRegion_[iRow]==1.0);
    numberInRow_[iRow]=0;
  }
  for (i=0;i<numberU_;i++) {
    CoinBigIndex k;
    CoinBigIndex j=startColumnU_[i];
    for (k=startColumnU_[i];k<startColumnU_[i]+numberInColumn_[i];k++) {
      int iRow=indexRowU_[k];
      if (!delRow[iRow]) {
	indexRowU_[j]=indexRowU_[k];
	elementU_[j++]=elementU_[k];
      }
    }
    numberInColumn_[i] = j-startColumnU_[i];
  }
  delete [] delRow;
  //space for cross reference
  CoinBigIndex *convertRowToColumn = convertRowToColumnU_;
  CoinBigIndex j = 0;
  CoinBigIndex *startRow = startRowU_;

  int iRow;
  for ( iRow = 0; iRow < numberRows_; iRow++ ) {
    startRow[iRow] = j;
    j += numberInRow_[iRow];
  }
  factorElements_=j;

  CoinZeroN ( numberInRow_, numberRows_ );

  for ( i = 0; i < numberRows_; i++ ) {
    CoinBigIndex start = startColumnU_[i];
    CoinBigIndex end = start + numberInColumn_[i];

    CoinBigIndex j;
    for ( j = start; j < end; j++ ) {
      int iRow = indexRowU_[j];
      int iLook = numberInRow_[iRow];

      numberInRow_[iRow] = iLook + 1;
      CoinBigIndex k = startRow[iRow] + iLook;

      indexColumnU_[k] = i;
      convertRowToColumn[k] = j;
    }
  }
}
// Updates part of column PFI (FTRAN)
void 
CoinFactorization::updateColumnPFI ( CoinIndexedVector * regionSparse) const
{
  double *region = regionSparse->denseVector (  );
  int * regionIndex = regionSparse->getIndices();
  double tolerance = zeroTolerance_;
  const CoinBigIndex *startColumn = startColumnU_+numberRows_;
  const int *indexRow = indexRowU_;
  const double *element = elementU_;
  int numberNonZero = regionSparse->getNumElements();
  int i;
#ifdef COIN_DEBUG
  for (i=0;i<numberNonZero;i++) {
    int iRow=regionIndex[i];
    assert (iRow>=0&&iRow<numberRows_);
    assert (region[iRow]);
  }
#endif
  const double *pivotRegion = pivotRegion_+numberRows_;
  const int *pivotColumn = pivotColumn_+numberRows_;

  for (i = 0 ; i <numberPivots_; i++ ) {
    int pivotRow=pivotColumn[i];
    double pivotValue = region[pivotRow];
    if (pivotValue) {
      if ( fabs ( pivotValue ) > tolerance ) {
	for (CoinBigIndex  j= startColumn[i] ; j < startColumn[i+1]; j++ ) {
	  int iRow = indexRow[j];
	  double oldValue = region[iRow];
	  double value = oldValue - pivotValue*element[j];
	  if (!oldValue) {
	    if (fabs(value)>tolerance) {
	      region[iRow]=value;
	      regionIndex[numberNonZero++]=iRow;
	    }
	  } else {
	    if (fabs(value)>tolerance) {
	      region[iRow]=value;
	    } else {
	      region[iRow]=COIN_INDEXED_REALLY_TINY_ELEMENT;
	    }
	  }
	}       
	pivotValue *= pivotRegion[i];
	region[pivotRow]=pivotValue;
      } else if (pivotValue) {
	region[pivotRow]=COIN_INDEXED_REALLY_TINY_ELEMENT;
      }
    }
  }
  regionSparse->setNumElements ( numberNonZero );
}
// Updates part of column transpose PFI (BTRAN),
    
void 
CoinFactorization::updateColumnTransposePFI ( CoinIndexedVector * regionSparse) const
{
  double *region = regionSparse->denseVector (  );
  int numberNonZero = regionSparse->getNumElements();
  int *index = regionSparse->getIndices (  );
  int i;
#ifdef COIN_DEBUG
  for (i=0;i<numberNonZero;i++) {
    int iRow=index[i];
    assert (iRow>=0&&iRow<numberRows_);
    assert (region[iRow]);
  }
#endif
  const int * pivotColumn = pivotColumn_+numberRows_;
  const double *pivotRegion = pivotRegion_+numberRows_;
  double tolerance = zeroTolerance_;
  
  const CoinBigIndex *startColumn = startColumnU_+numberRows_;
  const int *indexRow = indexRowU_;
  const double *element = elementU_;

  for (i=numberPivots_-1 ; i>=0; i-- ) {
    int pivotRow = pivotColumn[i];
    double pivotValue = region[pivotRow]*pivotRegion[i];
    for (CoinBigIndex  j= startColumn[i] ; j < startColumn[i+1]; j++ ) {
      int iRow = indexRow[j];
      double value = element[j];
      pivotValue -= value * region[iRow];
    }       
    //pivotValue *= pivotRegion[i];
    if ( fabs ( pivotValue ) > tolerance ) {
      if (!region[pivotRow])
	index[numberNonZero++] = pivotRow;
      region[pivotRow] = pivotValue;
    } else {
      if (region[pivotRow])
	region[pivotRow] = COIN_INDEXED_REALLY_TINY_ELEMENT;
    }       
  }       
  //set counts
  regionSparse->setNumElements ( numberNonZero );
}
/* Replaces one Column to basis for PFI
   returns 0=OK, 1=Probably OK, 2=singular, 3=no room
   Not sure what checkBeforeModifying means for PFI.
*/
int 
CoinFactorization::replaceColumnPFI ( CoinIndexedVector * regionSparse,
				      int pivotRow,
				      double alpha)
{
  CoinBigIndex *startColumn=startColumnU_+numberRows_;
  int *indexRow=indexRowU_;
  double *element=elementU_;
  int * pivotColumn = pivotColumn_+numberRows_;
  double * pivotRegion = pivotRegion_+numberRows_;
  // This has incoming column
  const double *region = regionSparse->denseVector (  );
  const int * index = regionSparse->getIndices();
  int numberNonZero = regionSparse->getNumElements();

  int iColumn = numberPivots_;

  if (!iColumn) 
    startColumn[0]=startColumnU_[maximumColumnsExtra_];
  CoinBigIndex start = startColumn[iColumn];
  
  //return at once if too many iterations
  if ( numberPivots_ >= maximumPivots_ ) {
    return 5;
  }       
  if ( lengthAreaU_ - ( start + numberNonZero ) < 0) {
    return 3;
  }   
  
  int i;
  if (numberPivots_) {
    if (fabs(alpha)<1.0e-5) {
      if (fabs(alpha)<1.0e-7)
	return 2;
      else
	return 1;
    }
  } else {
    if (fabs(alpha)<1.0e-8)
      return 2;
  }
  double pivotValue = 1.0/alpha;
  pivotRegion[iColumn]=pivotValue;
  double tolerance = zeroTolerance_;
  // Operations done before permute back
  if (regionSparse->packedMode()) {
    for ( i = 0; i < numberNonZero; i++ ) {
      int iRow = index[i];
      if (iRow!=pivotRow) {
	if ( fabs ( region[i] ) > tolerance ) {
	  indexRow[start]=pivotColumn_[iRow];
	  element[start++]=region[i]*pivotValue;
	}
      }
    }    
  } else {
    for ( i = 0; i < numberNonZero; i++ ) {
      int iRow = index[i];
      if (iRow!=pivotRow) {
	if ( fabs ( region[iRow] ) > tolerance ) {
	  indexRow[start]=pivotColumn_[iRow];
	  element[start++]=region[iRow]*pivotValue;
	}
      }
    }    
  }   
  numberPivots_++;
  numberNonZero=start-startColumn[iColumn];
  startColumn[numberPivots_]=start;
  totalElements_ += numberNonZero;
  pivotColumn[iColumn]=pivotColumn_[pivotRow];
  return 0;
}
