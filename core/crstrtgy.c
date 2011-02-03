   /*******************************************************/
   /*      "C" Language Integrated Production System      */
   /*                                                     */
   /*             CLIPS Version 6.30  10/19/06            */
   /*                                                     */
   /*         CONFLICT RESOLUTION STRATEGY MODULE         */
   /*******************************************************/

/*************************************************************/
/* Purpose: Used to determine where a new activation is      */
/*   placed on the agenda based on the current conflict      */
/*   resolution strategy (depth, breadth, mea, lex,          */
/*   simplicity, or complexity). Also provides the           */
/*   set-strategy and get-strategy commands.                 */
/*                                                           */
/* Principal Programmer(s):                                  */
/*      Gary D. Riley                                        */
/*                                                           */
/* Contributing Programmer(s):                               */
/*                                                           */
/* Revision History:                                         */
/*      6.23: Corrected compilation errors for files         */
/*            generated by constructs-to-c. DR0861           */
/*                                                           */
/*      6.24: Removed CONFLICT_RESOLUTION_STRATEGIES         */
/*            compilation flag.                              */
/*                                                           */
/*      6.30: Added salience groups to improve performance   */
/*            with large numbers of activations of different */
/*            saliences.                                     */
/*                                                           */
/*            Removed pseudo-facts used for not CEs.         */
/*                                                           */
/*************************************************************/

#define _CRSTRTGY_SOURCE_

#include <stdio.h>
#define _STDIO_INCLUDED_
#include <string.h>

#include "setup.h"

#if DEFRULE_CONSTRUCT

#include "constant.h"
#include "pattern.h"
#include "reteutil.h"
#include "argacces.h"
#include "agenda.h"
#include "envrnmnt.h"
#include "memalloc.h"

#include "crstrtgy.h"

#define GetMatchingItem(x,i) ((x->basis->binds[i].gm.theMatch != NULL) ? \
                              (x->basis->binds[i].gm.theMatch->matchingItem) : NULL)

/***************************************/
/* LOCAL INTERNAL FUNCTION DEFINITIONS */
/***************************************/

   static ACTIVATION             *PlaceDepthActivation(ACTIVATION *,struct salienceGroup *);
   static ACTIVATION             *PlaceBreadthActivation(ACTIVATION *,struct salienceGroup *);
   static ACTIVATION             *PlaceLEXActivation(void *,EXEC_STATUS,ACTIVATION *,struct salienceGroup *);
   static ACTIVATION             *PlaceMEAActivation(void *,EXEC_STATUS,ACTIVATION *,struct salienceGroup *);
   static ACTIVATION             *PlaceComplexityActivation(ACTIVATION *,struct salienceGroup *);
   static ACTIVATION             *PlaceSimplicityActivation(ACTIVATION *,struct salienceGroup *);
   static ACTIVATION             *PlaceRandomActivation(ACTIVATION *,struct salienceGroup *);
   static int                     ComparePartialMatches(void *,EXEC_STATUS,ACTIVATION *,ACTIVATION *);
   static char                   *GetStrategyName(int);
   static unsigned long long     *SortPartialMatch(void *,EXEC_STATUS,struct partialMatch *);
   
/******************************************************************/
/* PlaceActivation: Coordinates placement of an activation on the */
/*   Agenda based on the current conflict resolution strategy.    */
/******************************************************************/
globle void PlaceActivation(
  void *theEnv,
  EXEC_STATUS,
  ACTIVATION **whichAgenda,
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   ACTIVATION *placeAfter = NULL;

   /*================================================*/
   /* Set the flag which indicates that a change has */
   /* been made to the agenda.                       */
   /*================================================*/

   EnvSetAgendaChanged(theEnv,execStatus,TRUE);

   /*=============================================*/
   /* Determine the location where the activation */
   /* should be placed in the agenda based on the */
   /* current conflict resolution strategy.       */
   /*==============================================*/

   if (*whichAgenda != NULL) 
     {
      switch (AgendaData(theEnv)->Strategy)
        {
         case DEPTH_STRATEGY:
           placeAfter = PlaceDepthActivation(newActivation,theGroup);
           break;

         case BREADTH_STRATEGY:
           placeAfter = PlaceBreadthActivation(newActivation,theGroup);
           break;

         case LEX_STRATEGY:
           placeAfter = PlaceLEXActivation(theEnv,execStatus,newActivation,theGroup);
           break;

         case MEA_STRATEGY:
           placeAfter = PlaceMEAActivation(theEnv,execStatus,newActivation,theGroup);
           break;

         case COMPLEXITY_STRATEGY:
           placeAfter = PlaceComplexityActivation(newActivation,theGroup);
           break;

         case SIMPLICITY_STRATEGY:
           placeAfter = PlaceSimplicityActivation(newActivation,theGroup);
           break;

         case RANDOM_STRATEGY:
           placeAfter = PlaceRandomActivation(newActivation,theGroup);
           break;
        }
     } 
   else
     {
      theGroup->first = newActivation;
      theGroup->last = newActivation;
     }

   /*==============================================================*/
   /* Place the activation at the appropriate place in the agenda. */
   /*==============================================================*/

   if (placeAfter == NULL) /* then place it at the beginning of then agenda. */
     {
      newActivation->next = *whichAgenda;
      *whichAgenda = newActivation;
      if (newActivation->next != NULL) newActivation->next->prev = newActivation;
     }
   else /* insert it in the agenda. */
     {
      newActivation->next = placeAfter->next;
      newActivation->prev = placeAfter;
      placeAfter->next = newActivation;
      if (newActivation->next != NULL)
        { newActivation->next->prev = newActivation; }
     }
  }

/*******************************************************************/
/* PlaceDepthActivation: Determines the location in the agenda     */
/*    where a new activation should be placed for the depth        */
/*    strategy. Returns a pointer to the activation after which    */
/*    the new activation should be placed (or NULL if the          */
/*    activation should be placed at the beginning of the agenda). */
/*******************************************************************/
static ACTIVATION *PlaceDepthActivation(
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   ACTIVATION *lastAct, *actPtr;
   unsigned long long timetag;
     
   /*============================================*/
   /* Set up initial information for the search. */
   /*============================================*/

   timetag = newActivation->timetag;
   if (theGroup->prev == NULL)
     { lastAct = NULL; }
   else
     { lastAct = theGroup->prev->last; }

   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the activation is placed before      */
   /* activations with an equal or lower timetag (yielding    */
   /* depth first traversal).                                 */
   /*=========================================================*/

   actPtr = theGroup->first;
   while (actPtr != NULL)
     {
      if (timetag < actPtr->timetag)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else
        { break; }
     }

   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*******************************************************************/
/* PlaceBreadthActivation: Determines the location in the agenda   */
/*    where a new activation should be placed for the breadth      */
/*    strategy. Returns a pointer to the activation after which    */
/*    the new activation should be placed (or NULL if the          */
/*    activation should be placed at the beginning of the agenda). */
/*******************************************************************/
static ACTIVATION *PlaceBreadthActivation(
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   unsigned long long timetag;
   ACTIVATION *lastAct, *actPtr;

   /*============================================*/
   /* Set up initial information for the search. */
   /*============================================*/

   timetag = newActivation->timetag;
   if (theGroup->last == NULL)
     {    
      if (theGroup->prev == NULL)
        { lastAct = NULL; }
      else
        { lastAct = theGroup->prev->last; }
     }
   else
     { lastAct = theGroup->last; }

   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the activation is placed after       */
   /* activations with a lessor timetag (yielding breadth     */
   /* first traversal).                                       */
   /*=========================================================*/

   actPtr = theGroup->last;
   while (actPtr != NULL)
     {
      if (timetag < actPtr->timetag)
        {
         if (actPtr == theGroup->first)
           {
            if (theGroup->prev == NULL)
              { lastAct = NULL; }
            else
              { lastAct = theGroup->prev->last; }
            break;
           }
         else 
           { actPtr = actPtr->prev; }
        }
      else
        {
         lastAct = actPtr; 
         break; 
        }
     }
     
   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*******************************************************************/
/* PlaceLEXActivation: Determines the location in the agenda       */
/*    where a new activation should be placed for the lex          */
/*    strategy. Returns a pointer to the activation after which    */
/*    the new activation should be placed (or NULL if the          */
/*    activation should be placed at the beginning of the agenda). */
/*******************************************************************/
static ACTIVATION *PlaceLEXActivation(
  void *theEnv,
  EXEC_STATUS,
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   unsigned long long timetag;
   ACTIVATION *lastAct, *actPtr;
   int flag;

   /*============================================*/
   /* Set up initial information for the search. */
   /*============================================*/

   timetag = newActivation->timetag;
   if (theGroup->prev == NULL)
     { lastAct = NULL; }
   else
     { lastAct = theGroup->prev->last; }

   /*================================================*/
   /* Look first at the very end of the group to see */
   /* if the activation should be placed there.      */
   /*================================================*/
   
   actPtr = theGroup->last;
   if (actPtr != NULL)
     {
      flag = ComparePartialMatches(theEnv,execStatus,actPtr,newActivation);
      
      if ((flag == LESS_THAN) ||
          ((flag == EQUAL) &&  (timetag > actPtr->timetag)))
        {
         theGroup->last = newActivation; 
         
         return(actPtr);
        }
     }
     
   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the OPS5 lex strategy is used for    */
   /* determining placement.                                  */
   /*=========================================================*/

   actPtr = theGroup->first;
   while (actPtr != NULL)
     {
      flag = ComparePartialMatches(theEnv,execStatus,actPtr,newActivation);

      if (flag == LESS_THAN)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else if (flag == GREATER_THAN)
        { break; }
      else /* flag == EQUAL */
        {
         if (timetag > actPtr->timetag)
           {
            lastAct = actPtr;
            if (actPtr == theGroup->last)
              { break; }
            else 
              { actPtr = actPtr->next; }
           }
         else
           { break; }
        }
     }
     
   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*******************************************************************/
/* PlaceMEAActivation: Determines the location in the agenda       */
/*    where a new activation should be placed for the mea          */
/*    strategy. Returns a pointer to the activation after which    */
/*    the new activation should be placed (or NULL if the          */
/*    activation should be placed at the beginning of the agenda). */
/*******************************************************************/
static ACTIVATION *PlaceMEAActivation(
  void *theEnv,
  EXEC_STATUS,
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   unsigned long long timetag;
   ACTIVATION *lastAct, *actPtr;
   int flag;
   long long cWhoset = 0, oWhoset = 0;
   intBool cSet, oSet;

   /*============================================*/
   /* Set up initial information for the search. */
   /*============================================*/

   timetag = newActivation->timetag;
   if (theGroup->prev == NULL)
     { lastAct = NULL; }
   else
     { lastAct = theGroup->prev->last; }

   /*================================================*/
   /* Look first at the very end of the group to see */
   /* if the activation should be placed there.      */
   /*================================================*/
   
   actPtr = theGroup->last;
   if (actPtr != NULL)
     {
      if (GetMatchingItem(newActivation,0) != NULL)
        { 
         cWhoset = GetMatchingItem(newActivation,0)->timeTag; 
         cSet = TRUE;
        }
      else
        { cSet = FALSE; }
        
      if (GetMatchingItem(actPtr,0) != NULL)
        {
         oWhoset = GetMatchingItem(actPtr,0)->timeTag; 
         oSet = TRUE;
        }
      else
        { oSet = FALSE; }
        
      if ((cSet == FALSE) && (oSet == FALSE))  
        { flag = ComparePartialMatches(theEnv,execStatus,actPtr,newActivation); }
      else if ((cSet == TRUE) && (oSet == FALSE))
        { flag = GREATER_THAN; }
      else if ((cSet == FALSE) && (oSet == TRUE))
        { flag = LESS_THAN; }
      else if (oWhoset < cWhoset)
        { flag = GREATER_THAN; }
      else if (oWhoset > cWhoset)
        { flag = LESS_THAN; }
      else
        { flag = ComparePartialMatches(theEnv,execStatus,actPtr,newActivation); }

      if ((flag == LESS_THAN) ||
          ((flag == EQUAL) &&  (timetag > actPtr->timetag)))
        {
         theGroup->last = newActivation; 
         
         return(actPtr);
        }
     }

   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the OPS5 mea strategy is used for    */
   /* determining placement.                                  */
   /*=========================================================*/

   actPtr = theGroup->first;
   while (actPtr != NULL)
     {
      cWhoset = -1;
      oWhoset = -1;
      if (GetMatchingItem(newActivation,0) != NULL)
        { cWhoset = GetMatchingItem(newActivation,0)->timeTag; }
        
      if (GetMatchingItem(actPtr,0) != NULL)
        { oWhoset = GetMatchingItem(actPtr,0)->timeTag; }
        
      if (oWhoset < cWhoset)
        {
         if (cWhoset > 0) flag = GREATER_THAN;
         else flag = LESS_THAN;
        }
      else if (oWhoset > cWhoset)
        {
         if (oWhoset > 0) flag = LESS_THAN;
         else flag = GREATER_THAN;
        }
      else
        { flag = ComparePartialMatches(theEnv,execStatus,actPtr,newActivation); }

      if (flag == LESS_THAN)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else if (flag == GREATER_THAN)
        { break; }
      else /* flag == EQUAL */
        {
         if (timetag > actPtr->timetag)
           {
            lastAct = actPtr;
            if (actPtr == theGroup->last)
              { break; }
            else 
              { actPtr = actPtr->next; }
           }
         else
           { break; }
        }
     }
     
   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*********************************************************************/
/* PlaceComplexityActivation: Determines the location in the agenda  */
/*    where a new activation should be placed for the complexity     */
/*    strategy. Returns a pointer to the activation  after which the */
/*    new activation should be placed (or NULL if the activation     */
/*    should be placed at the beginning of the agenda).              */
/*********************************************************************/
static ACTIVATION *PlaceComplexityActivation(
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   int complexity;
   unsigned long long timetag;
   ACTIVATION *lastAct, *actPtr;

   /*========================================*/
   /* Set up initial information for search. */
   /*========================================*/

   timetag = newActivation->timetag;
   complexity = newActivation->theRule->complexity;
   if (theGroup->prev == NULL)
     { lastAct = NULL; }
   else
     { lastAct = theGroup->prev->last; }

   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the activation is placed before      */
   /* activations of equal or lessor complexity.              */
   /*=========================================================*/

   actPtr = theGroup->first;
   while (actPtr != NULL)
     {
      if (complexity < (int) actPtr->theRule->complexity)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else if (complexity > (int) actPtr->theRule->complexity)
        { break; }
      else if (timetag > actPtr->timetag)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else
        { break; }
     }
     
   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*********************************************************************/
/* PlaceSimplicityActivation: Determines the location in the agenda  */
/*    where a new activation should be placed for the simplicity     */
/*    strategy. Returns a pointer to the activation  after which the */
/*    new activation should be placed (or NULL if the activation     */
/*    should be placed at the beginning of the agenda).              */
/*********************************************************************/
static ACTIVATION *PlaceSimplicityActivation(
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   int complexity;
   unsigned long long timetag;
   ACTIVATION *lastAct, *actPtr;

   /*============================================*/
   /* Set up initial information for the search. */
   /*============================================*/

   timetag = newActivation->timetag;
   complexity = newActivation->theRule->complexity;
   if (theGroup->prev == NULL)
     { lastAct = NULL; }
   else
     { lastAct = theGroup->prev->last; }

   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the activation is placed after       */
   /* activations of equal or greater complexity.             */
   /*=========================================================*/

   actPtr = theGroup->first;
   while (actPtr != NULL)
     {
      if (complexity > (int) actPtr->theRule->complexity)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else if (complexity < (int) actPtr->theRule->complexity)
        { break; }
      else if (timetag > actPtr->timetag)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else
       { break; }
     }
     
   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*******************************************************************/
/* PlaceRandomActivation: Determines the location in the agenda    */
/*    where a new activation should be placed for the random       */
/*    strategy. Returns a pointer to the activation  after which   */
/*    the new activation should be placed (or NULL if the          */
/*    activation should be placed at the beginning of the agenda). */
/*******************************************************************/
static ACTIVATION *PlaceRandomActivation(
  ACTIVATION *newActivation,
  struct salienceGroup *theGroup)
  {
   int randomID;
   unsigned long long timetag;
   ACTIVATION *lastAct, *actPtr;

   /*============================================*/
   /* Set up initial information for the search. */
   /*============================================*/

   timetag = newActivation->timetag;
   randomID = newActivation->randomID;
   if (theGroup->prev == NULL)
     { lastAct = NULL; }
   else
     { lastAct = theGroup->prev->last; }

   /*=========================================================*/
   /* Find the insertion point in the agenda. The activation  */
   /* is placed before activations of lower salience and      */
   /* after activations of higher salience. Among activations */
   /* of equal salience, the placement of the activation is   */
   /* determined through the generation of a random number.   */
   /*=========================================================*/

   actPtr = theGroup->first;
   while (actPtr != NULL)
     {
      if (randomID > actPtr->randomID)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else if (randomID < actPtr->randomID)
       { break; }
      else if (timetag > actPtr->timetag)
        {
         lastAct = actPtr;
         if (actPtr == theGroup->last)
           { break; }
         else 
           { actPtr = actPtr->next; }
        }
      else
       { break; }
     }
     
   /*========================================*/
   /* Update the salience group information. */
   /*========================================*/
   
   if ((lastAct == NULL) || 
       ((theGroup->prev != NULL) && (theGroup->prev->last == lastAct)))
     { theGroup->first = newActivation; }
     
   if ((theGroup->last == NULL) || (theGroup->last == lastAct))
     { theGroup->last = newActivation; }

   /*===========================================*/
   /* Return the insertion point in the agenda. */
   /*===========================================*/

   return(lastAct);
  }

/*********************************************************/
/* SortPartialMatch: Creates an array of sorted timetags */
/*    in ascending order from a partial match.           */
/*********************************************************/
static unsigned long long *SortPartialMatch(
  void *theEnv,
  EXEC_STATUS,
  struct partialMatch *binds)
  {
   unsigned long long *nbinds;
   unsigned long long temp;
   int flag;
   unsigned j, k;

   /*====================================================*/
   /* Copy the array. Use 0 to represent the timetags of */
   /* negated patterns. Patterns matching fact/instances */
   /* should have timetags greater than 0.               */
   /*====================================================*/

   nbinds = (unsigned long long *) get_mem(theEnv,execStatus,sizeof(long long) * binds->bcount);
      
   for (j = 0; j < (unsigned) binds->bcount; j++)
     {
      if ((binds->binds[j].gm.theMatch != NULL) &&
          (binds->binds[j].gm.theMatch->matchingItem != NULL))
        { nbinds[j] = binds->binds[j].gm.theMatch->matchingItem->timeTag; }
      else
        { nbinds[j] = 0; }
     }

   /*=================*/
   /* Sort the array. */
   /*=================*/

   for (flag = TRUE, k = binds->bcount - 1;
        flag == TRUE;
        k--)
     {
      flag = FALSE;
      for (j = 0 ; j < k ; j++)
        {
         if (nbinds[j] < nbinds[j + 1])
           {
            temp = nbinds[j];
            nbinds[j] = nbinds[j+1];
            nbinds[j+1] = temp;
            flag = TRUE;
           }
        }
     }

   /*===================*/
   /* Return the array. */
   /*===================*/

   return(nbinds);
  }

/**************************************************************************/
/* ComparePartialMatches: Compares two activations using the lex conflict */
/*   resolution strategy to determine which activation should be placed   */
/*   first on the agenda. This lexicographic comparison function is used  */
/*   for both the lex and mea strategies.                                 */
/**************************************************************************/
static int ComparePartialMatches(
  void *theEnv,
  EXEC_STATUS,
  ACTIVATION *actPtr,
  ACTIVATION *newActivation)
  {
   int cCount, oCount, mCount, i;
   unsigned long long *basis1, *basis2;

   /*=================================================*/
   /* If the activation already on the agenda doesn't */
   /* have a set of sorted timetags, then create one. */
   /*=================================================*/

   basis1 = SortPartialMatch(theEnv,execStatus,newActivation->basis);
   basis2 = SortPartialMatch(theEnv,execStatus,actPtr->basis);
   
   /*==============================================================*/
   /* Determine the number of timetags in each of the activations. */
   /* The number of timetags to be compared is the lessor of these */
   /* two numbers.                                                 */
   /*==============================================================*/

   cCount = newActivation->basis->bcount;
   oCount = actPtr->basis->bcount;
 
   if (oCount > cCount) mCount = cCount;
   else mCount = oCount;

   /*===========================================================*/
   /* Compare the sorted timetags one by one until there are no */
   /* more timetags to compare or the timetags being compared   */
   /* are not equal. If the timetags aren't equal, then the     */
   /* activation containing the larger timetag is placed before */
   /* the activation containing the smaller timetag.            */
   /*===========================================================*/

   for (i = 0 ; i < mCount ; i++)
     {
      if (basis1[i] < basis2[i])
        { 
         rtn_mem(theEnv,execStatus,sizeof(long long) * cCount,basis1);
         rtn_mem(theEnv,execStatus,sizeof(long long) * oCount,basis2);
         return(LESS_THAN); 
        }
      else if (basis1[i] > basis2[i])
        { 
         rtn_mem(theEnv,execStatus,sizeof(long long) * cCount,basis1);
         rtn_mem(theEnv,execStatus,sizeof(long long) * oCount,basis2);
         return(GREATER_THAN); 
        }
     }
  
   rtn_mem(theEnv,execStatus,sizeof(long long) * cCount,basis1);
   rtn_mem(theEnv,execStatus,sizeof(long long) * oCount,basis2);

   /*==========================================================*/
   /* If the sorted timetags are identical up to the number of */
   /* timetags contained in the smaller partial match, then    */
   /* the activation containing more timetags should be        */
   /* placed before the activation containing fewer timetags.  */
   /*==========================================================*/

   if (cCount < oCount) return(LESS_THAN);
   else if (cCount > oCount) return(GREATER_THAN);

   /*=========================================================*/
   /* If the sorted partial matches for both activations are  */
   /* identical (containing the same number and values of     */
   /* timetags), then the activation associated with the rule */
   /* having the highest complexity is placed before the      */
   /* other partial match.                                    */
   /*=========================================================*/

   if (newActivation->theRule->complexity < actPtr->theRule->complexity)
     { return(LESS_THAN); }
   else if (newActivation->theRule->complexity > actPtr->theRule->complexity)
     { return(GREATER_THAN); }

   /*================================================*/
   /* The two partial matches are equal for purposes */
   /* of placement on the agenda for the lex and mea */
   /* conflict resolution strategies.                */
   /*================================================*/

   return(EQUAL);
  }

/************************************/
/* EnvSetStrategy: C access routine */
/*   for the set-strategy command.  */
/************************************/
globle int EnvSetStrategy(
  void *theEnv,
  EXEC_STATUS,
  int value)
  {
   int oldStrategy;
   
   oldStrategy = AgendaData(theEnv)->Strategy;
   AgendaData(theEnv)->Strategy = value;

   if (oldStrategy != AgendaData(theEnv)->Strategy) EnvReorderAgenda(theEnv,execStatus,NULL);

   return(oldStrategy);
  }

/************************************/
/* EnvGetStrategy: C access routine */
/*   for the get-strategy command.  */
/************************************/
globle int EnvGetStrategy(
  void *theEnv,
  EXEC_STATUS)
  {
   return(AgendaData(theEnv)->Strategy);
  }

/********************************************/
/* GetStrategyCommand: H/L access routine   */
/*   for the get-strategy command.          */
/********************************************/
globle void *GetStrategyCommand(
  void *theEnv,
  EXEC_STATUS)
  {
   EnvArgCountCheck(theEnv,execStatus,"get-strategy",EXACTLY,0);

   return((SYMBOL_HN *) EnvAddSymbol(theEnv,execStatus,GetStrategyName(EnvGetStrategy(theEnv,execStatus))));
  }

/********************************************/
/* SetStrategyCommand: H/L access routine   */
/*   for the set-strategy command.          */
/********************************************/
globle void *SetStrategyCommand(
  void *theEnv,
  EXEC_STATUS)
  {
   DATA_OBJECT argPtr;
   char *argument;
   int oldStrategy;
   
   oldStrategy = AgendaData(theEnv)->Strategy;

   /*=====================================================*/
   /* Check for the correct number and type of arguments. */
   /*=====================================================*/

   if (EnvArgCountCheck(theEnv,execStatus,"set-strategy",EXACTLY,1) == -1)
     { return((SYMBOL_HN *) EnvAddSymbol(theEnv,execStatus,GetStrategyName(EnvGetStrategy(theEnv,execStatus)))); }

   if (EnvArgTypeCheck(theEnv,execStatus,"set-strategy",1,SYMBOL,&argPtr) == FALSE)
     { return((SYMBOL_HN *) EnvAddSymbol(theEnv,execStatus,GetStrategyName(EnvGetStrategy(theEnv,execStatus)))); }

   argument = DOToString(argPtr);

   /*=============================================*/
   /* Set the strategy to the specified strategy. */
   /*=============================================*/

   if (strcmp(argument,"depth") == 0)
     { EnvSetStrategy(theEnv,execStatus,DEPTH_STRATEGY); }
   else if (strcmp(argument,"breadth") == 0)
     { EnvSetStrategy(theEnv,execStatus,BREADTH_STRATEGY); }
   else if (strcmp(argument,"lex") == 0)
     { EnvSetStrategy(theEnv,execStatus,LEX_STRATEGY); }
   else if (strcmp(argument,"mea") == 0)
     { EnvSetStrategy(theEnv,execStatus,MEA_STRATEGY); }
   else if (strcmp(argument,"complexity") == 0)
     { EnvSetStrategy(theEnv,execStatus,COMPLEXITY_STRATEGY); }
   else if (strcmp(argument,"simplicity") == 0)
     { EnvSetStrategy(theEnv,execStatus,SIMPLICITY_STRATEGY); }
   else if (strcmp(argument,"random") == 0)
     { EnvSetStrategy(theEnv,execStatus,RANDOM_STRATEGY); }
   else
     {
      ExpectedTypeError1(theEnv,execStatus,"set-strategy",1,
      "symbol with value depth, breadth, lex, mea, complexity, simplicity, or random");
      return((SYMBOL_HN *) EnvAddSymbol(theEnv,execStatus,GetStrategyName(EnvGetStrategy(theEnv,execStatus))));
     }

   /*=======================================*/
   /* Return the old value of the strategy. */
   /*=======================================*/

   return((SYMBOL_HN *) EnvAddSymbol(theEnv,execStatus,GetStrategyName(oldStrategy)));
  }

/**********************************************************/
/* GetStrategyName: Given the integer value corresponding */
/*   to a specified strategy, return a character string   */
/*   of the strategy's name.                              */
/**********************************************************/
static char *GetStrategyName(
  int strategy)
  {
   char *sname;

   switch (strategy)
     {
      case DEPTH_STRATEGY:
        sname = "depth";
        break;
      case BREADTH_STRATEGY:
        sname = "breadth";
        break;
      case LEX_STRATEGY:
        sname = "lex";
        break;
      case MEA_STRATEGY:
        sname = "mea";
        break;
      case COMPLEXITY_STRATEGY:
        sname = "complexity";
        break;
      case SIMPLICITY_STRATEGY:
        sname = "simplicity";
        break;
      case RANDOM_STRATEGY:
        sname = "random";
        break;
      default:
        sname = "unknown";
        break;
     }

   return(sname);
  }

#endif /* DEFRULE_CONSTRUCT */

