#ifndef _DOHPMESH_H
#define _DOHPMESH_H

#include <petscsys.h>
#include <dohptype.h>
#include <dohpjacobi.h>

#ifdef dHAVE_ITAPS_REL
#  include <iGeom.h>
#  include <iRel.h>
#endif

dEXTERN_C_BEGIN

typedef struct _p_dMeshPacker *dMeshPacker;
typedef struct _p_dMesh *dMesh;

extern dClassId dMESH_CLASSID;
extern PetscBool dMeshRegisterAllCalled;
extern PetscLogEvent dLOG_MeshLoad;
extern PetscFList dMeshList;

extern const char *const iBase_ErrorString[];
extern const char *const iBase_TagValueTypeName[];

/* Unfortunately the explicit `mesh' is necessary to get a useful error string */
#define dICHK(mesh,err) do {                                            \
    if (PetscUnlikely(err)) {                                           \
      dErr _l_ret = err;                                                \
      char _l_desc[512] = "Description not available";                  \
      iMesh_getDescription(mesh,_l_desc,sizeof(_l_desc));               \
      dERROR(PETSC_COMM_SELF,1,"iMesh(%d) %s: %s",_l_ret,iBase_ErrorString[_l_ret],_l_desc); \
    }                                                                   \
  } while (0)
#define dIGCHK(geom,err) do {                                           \
    if (PetscUnlikely(err)) {                                           \
      dErr _l_ret = err;                                                \
      char _l_desc[512] = "Description not available";                  \
      iGeom_getDescription(geom,_l_desc,sizeof(_l_desc));               \
      dERROR(PETSC_COMM_SELF,1,"iGeom(%d) %s: %s",_l_ret,iBase_ErrorString[_l_ret],_l_desc); \
    }                                                                   \
  } while (0)
#define dIRCHK(rel,err) do {                                           \
    if (PetscUnlikely(err)) {                                           \
      dErr _l_ret = err;                                                \
      char _l_desc[512] = "Description not available";                  \
      iRel_getDescription(rel,_l_desc,sizeof(_l_desc));                 \
      dERROR(PETSC_COMM_SELF,1,"iRel(%d) %s: %s",_l_ret,iBase_ErrorString[_l_ret],_l_desc); \
    }                                                                   \
  } while (0)


typedef struct {
  void *v;
  dMeshInt a,s;
} MeshListData;

typedef struct {
  dMeshReal *v;
  dMeshInt a, s;
} MeshListReal;

typedef struct {
  dMeshInt *v, a, s;
} MeshListInt;

typedef struct {
  dMeshEH *v;
  dMeshInt a, s;
} MeshListEH;

typedef struct {
  dMeshESH *v;
  dMeshInt a,s;
} MeshListESH;

typedef struct {
  dMeshTag *v;
  dMeshInt a,s;
} MeshListTag;

/* Use this macro to zero a MeshListXXX, i.e. MeshListInt a=MLZ; */
#define MLZ {0,0,0}
#define MeshListFree(m) ((m).a && (free((m).v),(m).v=0,(m).a=0,(m).s=0,0))
#define MeshListMalloc(m,n) ( m ? ((n).s=0,(n).a=m,(n).v=malloc((n).a*sizeof(n.v[0])),!(n.v)) : MeshListFree(n))
#define MLREF(m) &(m).v,&(m).a,&(m).s

typedef struct {
  dInt start, stride, end;
} DohpLoopBounds;

/* name tag on manifold sets, NEUMANN_SET is the default when coming from Cubit */
#define dTAG_MANIFOLD_ID       "NEUMANN_SET"
#define dTAG_SENSE             "SENSE"
#define dTAG_EMPTYSET          "EMPTYSET"
#define dTAG_ORDERED_SUBDOMAIN "ORDERED_SUBDOMAIN"
#define dTAG_PARTITION         "PARALLEL_PARTITION"

typedef enum {dMESHSET_UNORDERED = 0,dMESHSET_ORDERED = 1} dMeshSetOrdering;
typedef enum {dMESHGEN_NONE, dMESHGEN_BLOCK} dMeshGenType;
extern const char *const dMeshGenTypes[];

#define dMeshType char *
#define dMESHPACK   "pack"
#define dMESHSERIAL "serial"

extern dErr dMeshListIntView(MeshListInt*,const char*);
extern dErr dMeshListEHView(MeshListEH*,const char*);

extern dErr dMeshOrientLoopBounds_Quad(dInt orient, const dInt *size, DohpLoopBounds *l);
extern dErr dMeshOrientLoopBounds_Line(dInt orient, const dInt *size, DohpLoopBounds *l);
extern dErr dMeshLoopBounds_Quad(const dInt *size, dInt edge, DohpLoopBounds *l);
extern dErr dMeshLoopBounds_Hex(const dInt *size, dInt face, DohpLoopBounds *l);

extern dErr dMeshGetLocalNodeNumbering(dMesh,dInt,dInt*,dInt*);
extern dErr dMeshGetTagName(dMesh m,dMeshTag tag,char **name);
extern dErr dMeshLoad(dMesh m);
extern dErr dMeshInferIntermediateAdjacencies(dMesh mesh);
extern dErr dMeshSetInFile(dMesh,const char fname[],const char opt[]);
extern dErr dMeshGetRoot(dMesh mesh,dMeshESH *inroot);
extern dErr dMeshSetDuplicateEntsOnly(dMesh mesh,dMeshESH set,dMeshESH *copy);
extern dErr dMeshSetGetOrdering(dMesh,dMeshESH,dMeshSetOrdering*);
extern dErr dMeshCreate(MPI_Comm comm,dMesh *inm);
extern dErr dMeshDestroy(dMesh*);
extern dErr dMeshView(dMesh,dViewer);
extern dErr dMeshDumpDHM(dMesh,const char *filename,...);
extern dErr dMeshSetView(dMesh m,dMeshESH root,PetscViewer viewer);
extern dErr dMeshRegisterAll(const char path[]);
#if defined PETSC_USE_DYNAMIC_LIBRARIES
#  define dMeshRegisterDynamic(a,b,c,d) dMeshRegister(a,b,c,0)
#else
#  define dMeshRegisterDynamic(a,b,c,d) dMeshRegister(a,b,c,d)
#endif
extern dErr dMeshRegister(const char[],const char[],const char[],dErr(*)(dMesh));
extern dErr dMeshSetType(dMesh,const dMeshType);
extern dErr dMeshInitializePackage(const char[]);
extern dErr dMeshFinalizePackage(void);
extern dErr dMeshCreateRuleTagIsotropic(dMesh,dMeshESH,const char*,dInt,dMeshTag*);
extern dErr dMeshDestroyRuleTag(dMesh,dMeshTag);
extern dErr dMeshGetInstance(dMesh,iMesh_Instance*);
extern dErr dMeshGetNumEnts(dMesh,dMeshESH,dEntType,dEntTopology,dInt*);
extern dErr dMeshGetEnts(dMesh,dMeshESH,dEntType,dEntTopology,dMeshEH[],dInt,dInt*);
extern dErr dMeshGetNumSubsets(dMesh,dMeshESH,dInt,dInt*);
extern dErr dMeshGetSubsets(dMesh,dMeshESH,dInt,dMeshESH[],dInt,dInt*);
extern dErr dMeshGetEntsOff(dMesh,dMeshESH,dInt*,dMeshEH**);
extern dErr dMeshSetFilterEnts(dMesh,dMeshESH,dEntType,dEntTopology,dMeshESH*);

extern dErr dMeshGetTag(dMesh mesh,const char name[],dMeshTag *intag);
extern dErr dMeshTagDestroy(dMesh mesh,dMeshTag tag);
extern dErr dMeshTagCreate(dMesh mesh,const char[],dInt count,dDataType type,dMeshTag *intag);
extern dErr dMeshTagCreateTemp(dMesh mesh,const char[],dInt count,dDataType type,dMeshTag *intag);
extern dErr dMeshTagSetData(dMesh mesh,dMeshTag tag,const dMeshEH ents[],dInt ecount,const void *data,dInt count,dDataType type);
extern dErr dMeshTagGetData(dMesh mesh,dMeshTag tag,const dMeshEH ents[],dInt ecount,void *data,dInt count,dDataType type);
extern dErr dMeshTagSSetData(dMesh mesh,dMeshTag tag,const dMeshESH esets[],dInt ecount,const void *data,dInt count,dDataType type);
extern dErr dMeshTagSGetData(dMesh mesh,dMeshTag tag,const dMeshESH esets[],dInt ecount,void *data,dInt count,dDataType type);
extern dErr dMeshGetTaggedSet(dMesh,dMeshTag,const void*,dMeshESH*);
extern dErr dMeshGetTaggedSets(dMesh,dMeshTag,dInt,const void *,dInt *,dMeshESH **);
extern dErr dMeshSetFromOptions(dMesh);
extern dErr dMeshTagBcast(dMesh mesh,dMeshTag tag);
extern dErr dMeshSetCreate(dMesh,dMeshSetOrdering,dMeshESH*);
extern dErr dMeshSetDestroy(dMesh,dMeshESH);
extern dErr dMeshSetAddEnts(dMesh,dMeshESH,const dMeshEH*,dInt);
extern dErr dMeshEntClassifyExclusive(dMesh mesh,dMeshEH ent,dInt nsets,const dMeshESH sets[],dInt *member);
extern dErr dMeshClassifyCountInt(dMesh mesh,dInt nents,const dMeshEH ents[],const dInt values[],dInt nsets,const dMeshESH sets[],dInt counts[]);
extern dErr dMeshGetStatus(dMesh,const dMeshEH[],dInt,dEntStatus[]);
extern dErr dMeshGetTopo(dMesh,dInt,const dMeshEH[],dEntTopology[]);
extern dErr dMeshGetAdjacency(dMesh,dMeshESH,dMeshAdjacency*);
extern dErr dMeshRestoreAdjacency(dMesh,dMeshESH,dMeshAdjacency*);
extern dErr dMeshGetVertexCoords(dMesh,dInt,const dMeshEH[],const dReal**);
extern dErr dMeshRestoreVertexCoords(dMesh,dInt,const dMeshEH[],const dReal**);
extern dErr dMeshGetAdjVertexCoords(dMesh,dInt,const dMeshEH[],const dInt**,const dReal**);
extern dErr dMeshRestoreAdjVertexCoords(dMesh,dInt,const dMeshEH[],const dInt**,const dReal**);
extern dErr dMeshPartitionOnOwnership(dMesh,dMeshEH[],dInt,dInt*);
extern dErr dMeshMorph(dMesh,void(*morph)(void*,double*),void*);
extern dErr dMeshSetClosure(dMesh,dMeshESH);
extern dErr dMeshGenerateBlock(dMesh mesh,dMeshESH root,PetscBool do_geom);
extern dErr dMeshSetGenerate(dMesh,dMeshGenType);

#ifdef dHAVE_ITAPS_REL
extern dErr dMeshSetGeometryRelation(dMesh,iGeom_Instance,iRel_Instance);
extern dErr dMeshGetGeometryRelation(dMesh,iGeom_Instance*,iRel_Instance*);
#endif

dEXTERN_C_END

#endif
