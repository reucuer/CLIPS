
   /*******************************************************/
   /*      "C" Language Integrated Production System      */
   /*                                                     */
   /*             CLIPS Version 6.20  01/31/02            */
   /*                                                     */
   /*     FACT PATTERN NETWORK CONSTRUCTS-TO-C MODULE     */
   /*******************************************************/

/*************************************************************/
/* Purpose: Implements the constructs-to-c feature for the   */
/*    fact pattern network.                                  */
/*                                                           */
/* Principal Programmer(s):                                  */
/*      Gary D. Riley                                        */
/*                                                           */
/* Contributing Programmer(s):                               */
/*                                                           */
/* Revision History:                                         */
/*                                                           */
/*************************************************************/

#define _FACTCMP_SOURCE_

#include "setup.h"

#if DEFRULE_CONSTRUCT && (! RUN_TIME) && DEFTEMPLATE_CONSTRUCT && CONSTRUCT_COMPILER

#define FactPrefix() ArbitraryPrefix(FactData(theEnv,execStatus)->FactCodeItem,0)

#include <stdio.h>
#define _STDIO_INCLUDED_

#include "factbld.h"
#include "conscomp.h"
#include "factcmp.h"
#include "tmpltdef.h"
#include "envrnmnt.h"

/***************************************/
/* LOCAL INTERNAL FUNCTION DEFINITIONS */
/***************************************/

   static int                     PatternNetworkToCode(void *,EXEC_STATUS,char *,char *,char *,int,FILE *,int,int);
   static void                    BeforePatternNetworkToCode(void *,EXEC_STATUS);
   static struct factPatternNode *GetNextPatternNode(struct factPatternNode *);
   static void                    CloseNetworkFiles(void *,EXEC_STATUS,FILE *,int);
   static void                    PatternNodeToCode(void *,EXEC_STATUS,FILE *,struct factPatternNode *,int,int);

/**************************************************************/
/* FactPatternsCompilerSetup: Initializes the constructs-to-c */
/*   command for use with the fact pattern network.           */
/**************************************************************/
globle void FactPatternsCompilerSetup(  
  void *theEnv,
  EXEC_STATUS)
  {
   FactData(theEnv,execStatus)->FactCodeItem = AddCodeGeneratorItem(theEnv,execStatus,"facts",0,BeforePatternNetworkToCode,
                                       NULL,PatternNetworkToCode,1);
  }

/****************************************************************/
/* BeforePatternNetworkToCode: Assigns each pattern node with a */
/*   unique ID which will be used for pointer references when   */
/*   the data structures are written to a file as C code        */
/****************************************************************/
static void BeforePatternNetworkToCode(
  void *theEnv,
  EXEC_STATUS)
  {
   int whichPattern = 0;
   int whichDeftemplate = 0;
   struct defmodule *theModule;
   struct deftemplate *theDeftemplate;
   struct factPatternNode *thePattern;

   /*===========================*/
   /* Loop through each module. */
   /*===========================*/

   for (theModule = (struct defmodule *) EnvGetNextDefmodule(theEnv,execStatus,NULL);
        theModule != NULL;
        theModule = (struct defmodule *) EnvGetNextDefmodule(theEnv,execStatus,theModule))
     {
      /*=========================*/
      /* Set the current module. */
      /*=========================*/

      EnvSetCurrentModule(theEnv,execStatus,(void *) theModule);

      /*======================================================*/
      /* Loop through each deftemplate in the current module. */
      /*======================================================*/

      for (theDeftemplate = (struct deftemplate *) EnvGetNextDeftemplate(theEnv,execStatus,NULL);
           theDeftemplate != NULL;
           theDeftemplate = (struct deftemplate *) EnvGetNextDeftemplate(theEnv,execStatus,theDeftemplate))
        {
         /*=================================================*/
         /* Assign each pattern node in the pattern network */
         /* for the deftemplate a unique integer ID.        */
         /*=================================================*/

         theDeftemplate->header.bsaveID = whichDeftemplate++;
         for (thePattern = theDeftemplate->patternNetwork;
              thePattern != NULL;
              thePattern = GetNextPatternNode(thePattern))
           { thePattern->bsaveID = whichPattern++; }
        }
     }
  }

/********************************************************************/
/* GetNextPatternNode: Returns the next node in a pattern network   */
/*   tree. The next node is computed using a depth first traversal. */
/********************************************************************/
static struct factPatternNode *GetNextPatternNode(
  struct factPatternNode *thePattern)
  {
   /*=========================================*/
   /* If it's possible to go deeper into the  */
   /* tree, then move down to the next level. */
   /*=========================================*/

   if (thePattern->nextLevel != NULL) return(thePattern->nextLevel);

   /*========================================*/
   /* Keep backing up toward the root of the */
   /* tree until a side branch can be taken. */
   /*========================================*/

   while (thePattern->rightNode == NULL)
     {
      /*========================================*/
      /* Back up to check the next side branch. */
      /*========================================*/

      thePattern = thePattern->lastLevel;

      /*======================================*/
      /* If we branched up from the root, the */
      /* entire tree has been traversed.      */
      /*======================================*/

      if (thePattern == NULL) return(NULL);
     }

   /*==================================*/
   /* Move on to the next side branch. */
   /*==================================*/

   return(thePattern->rightNode);
  }

/********************************************************************/
/* PatternNetworkToCode: Produces the fact pattern network code for */
/*   a run-time module created using the constructs-to-c function.  */
/********************************************************************/
static int PatternNetworkToCode(
  void *theEnv,
  EXEC_STATUS,
  char *fileName,
  char *pathName,
  char *fileNameBuffer,
  int fileID,
  FILE *headerFP,
  int imageID,
  int maxIndices)
  {
   int fileCount = 1;
   struct defmodule *theModule;
   struct deftemplate *theTemplate;
   struct factPatternNode *thePatternNode;
   int networkArrayCount = 0, networkArrayVersion = 1;
   FILE *networkFile = NULL;

   /*===========================================================*/
   /* Include the appropriate fact pattern network header file. */
   /*===========================================================*/

   fprintf(headerFP,"#include \"factbld.h\"\n");

   /*===============================*/
   /* Loop through all the modules. */
   /*===============================*/

   for (theModule = (struct defmodule *) EnvGetNextDefmodule(theEnv,execStatus,NULL);
        theModule != NULL;
        theModule = (struct defmodule *) EnvGetNextDefmodule(theEnv,execStatus,theModule))
     {
      /*=========================*/
      /* Set the current module. */
      /*=========================*/

      EnvSetCurrentModule(theEnv,execStatus,(void *) theModule);

      /*======================================*/
      /* Loop through all of the deftemplates */
      /* in the current module.               */
      /*======================================*/

      for (theTemplate = (struct deftemplate *) EnvGetNextDeftemplate(theEnv,execStatus,NULL);
           theTemplate != NULL;
           theTemplate = (struct deftemplate *) EnvGetNextDeftemplate(theEnv,execStatus,theTemplate))
        {
         /*======================================================*/
         /* Loop through each pattern node in the deftemplate's  */
         /* pattern network writing its C code representation to */
         /* the file as it is traversed.                         */
         /*======================================================*/

         for (thePatternNode = theTemplate->patternNetwork;
              thePatternNode != NULL;
              thePatternNode = GetNextPatternNode(thePatternNode))
           {
            networkFile = OpenFileIfNeeded(theEnv,execStatus,networkFile,fileName,pathName,fileNameBuffer,fileID,imageID,&fileCount,
                                         networkArrayVersion,headerFP,
                                         "struct factPatternNode",FactPrefix(),FALSE,NULL);
            if (networkFile == NULL)
              {
               CloseNetworkFiles(theEnv,execStatus,networkFile,maxIndices);
               return(0);
              }

            PatternNodeToCode(theEnv,execStatus,networkFile,thePatternNode,imageID,maxIndices);
            networkArrayCount++;
            networkFile = CloseFileIfNeeded(theEnv,execStatus,networkFile,&networkArrayCount,
                                            &networkArrayVersion,maxIndices,NULL,NULL);
           }
        }
     }

   /*==============================*/
   /* Close any C files left open. */
   /*==============================*/

   CloseNetworkFiles(theEnv,execStatus,networkFile,maxIndices);

   /*===============================*/
   /* Return TRUE to indicate the C */
   /* code was successfully saved.  */
   /*===============================*/

   return(TRUE);
  }

/****************************************************************/
/* CloseNetworkFiles: Closes all of the C files created for the */
/*   fact pattern network. Called when an error occurs or when  */
/*   the fact pattern network data structures have all been     */
/*   written to the files.                                      */
/****************************************************************/
static void CloseNetworkFiles(
  void *theEnv,
  EXEC_STATUS,
  FILE *networkFile,
  int maxIndices)
  {
   int count = maxIndices;
   int arrayVersion = 0;

   if (networkFile != NULL)
     {
      CloseFileIfNeeded(theEnv,execStatus,networkFile,&count,&arrayVersion,maxIndices,NULL,NULL);
     }
  }

/************************************************************/
/* PatternNodeToCode: Writes the C code representation of a */
/*   single fact pattern node slot to the specified file.   */
/************************************************************/
static void PatternNodeToCode(
  void *theEnv,
  EXEC_STATUS,
  FILE *theFile,
  struct factPatternNode *thePatternNode,
  int imageID,
  int maxIndices)
  {
   fprintf(theFile,"{");

   /*=====================*/
   /* Pattern Node Header */
   /*=====================*/

   PatternNodeHeaderToCode(theEnv,execStatus,theFile,&thePatternNode->header,imageID,maxIndices);

   /*========================*/
   /* Field and Slot Indices */
   /*========================*/

   fprintf(theFile,",0,%d,%d,%d,",thePatternNode->whichField,
                             thePatternNode->whichSlot,
                             thePatternNode->leaveFields);

   /*===============*/
   /* Network Tests */
   /*===============*/

   PrintHashedExpressionReference(theEnv,execStatus,theFile,thePatternNode->networkTest,imageID,maxIndices);

   /*============*/
   /* Next Level */
   /*============*/

   if (thePatternNode->nextLevel == NULL)
     { fprintf(theFile,",NULL,"); }
   else
     {
      fprintf(theFile,",&%s%d_%ld[%ld],",FactPrefix(),
                 imageID,(thePatternNode->nextLevel->bsaveID / maxIndices) + 1,
                         thePatternNode->nextLevel->bsaveID % maxIndices);
     }

   /*============*/
   /* Last Level */
   /*============*/

   if (thePatternNode->lastLevel == NULL)
     { fprintf(theFile,"NULL,"); }
   else
     {
      fprintf(theFile,"&%s%d_%ld[%ld],",FactPrefix(),
              imageID,(thePatternNode->lastLevel->bsaveID / maxIndices) + 1,
                   thePatternNode->lastLevel->bsaveID % maxIndices);
     }

   /*===========*/
   /* Left Node */
   /*===========*/

   if (thePatternNode->leftNode == NULL)
     { fprintf(theFile,"NULL,"); }
   else
     {
      fprintf(theFile,"&%s%d_%ld[%ld],",FactPrefix(),
             imageID,(thePatternNode->leftNode->bsaveID / maxIndices) + 1,
                  thePatternNode->leftNode->bsaveID % maxIndices);
     }

   /*============*/
   /* Right Node */
   /*============*/

   if (thePatternNode->rightNode == NULL)
     { fprintf(theFile,"NULL}"); }
   else
     {
      fprintf(theFile,"&%s%d_%ld[%ld]}",FactPrefix(),
            imageID,(thePatternNode->rightNode->bsaveID / maxIndices) + 1,
                thePatternNode->rightNode->bsaveID % maxIndices);
     }
  }

/**********************************************************/
/* FactPatternNodeReference: Prints C code representation */
/*   of a fact pattern node data structure reference.     */
/**********************************************************/
globle void FactPatternNodeReference(
  void *theEnv,
  EXEC_STATUS,
  void *theVPattern,
  FILE *theFile,
  int imageID,
  int maxIndices)
  {
   struct factPatternNode *thePattern = (struct factPatternNode *) theVPattern;

   if (thePattern == NULL) fprintf(theFile,"NULL");
   else
     {
      fprintf(theFile,"&%s%d_%ld[%ld]",FactPrefix(),
                    imageID,(thePattern->bsaveID / maxIndices) + 1,
                            thePattern->bsaveID % maxIndices);
     }
  }

#endif /* DEFRULE_CONSTRUCT && (! RUN_TIME) && DEFTEMPLATE_CONSTRUCT && CONSTRUCT_COMPILER */


