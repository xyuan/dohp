#if !defined(_DOHP_H)
#define _DOHP_H

#include "petscsnes.h"
#include "dohpquotient.h"

PETSC_EXTERN_CXX_BEGIN

// Needed by (automatic) user code to perform element operations

// Requested derivatives
enum DohpDeriv {
  DERIV_BASIS = 0x1,
  DERIV_X     = 0x2,
  DERIV_Y     = 0x4,
  DERIV_Z     = 0x8
};

#define dNAME_LEN 128

/* Function to define the quadrature order on a given mesh. */
typedef dErr (*dQuotientFunction1)(const dReal*,dInt*);
/* Function to define an approximation space given a mesh and a quadrature order. */
typedef dErr (*DohpMFSFunction1)(const dReal*,const dInt*,dInt*);

/* Central user-visible objects. */
typedef struct p_dohpDM *DohpDM;
typedef struct p_dohpMFS *DohpMFS;
typedef struct p_dohpWF *DohpWF;
typedef struct p_dohpBlock* DohpBlock;

typedef struct p_dohpEFS* DohpEFS;

/* Quadrature rules.  Currently we support tensor-product type Gauss, Gauss-Lobatto, and
* Gauss-Radau.  Tensor product spaces and quadrature rules are central to the efficiency of the
* method, but the implementation should become private. */
typedef struct {
  dReal   *coord;
  dReal   *weight;
  dInt     size;
} dRule_Line;

typedef struct {
  dRule_Line l[2];
} dRule_Quad;

typedef struct {
  dRule_Line l[3];
} dRule_Hex;

/* Element coordinate mappings, these should become private eventually. */

/* Example implementation, parametric maps normally need *much* more space since the Jacobian has
* different values at each quadrature point. */
typedef struct {
  dReal jac[9];
  dReal jinv[9];
  dReal jdet;
} DohpEMap_Affine3;

/* We can do a poor man's parametric map by using vertex coordinates and computing Jacobians at
* quadrature points on the fly.  For this, we need only store the vertex coordinates in interlaced
* ordering. */
typedef struct {
  dReal vtx[2*3];
} DohpEMap_Line;

typedef struct {
  dReal vtx[4*3];
} DohpEMap_Quad;

typedef struct {
  dReal vtx[8*3];
} DohpEMap_Hex;

/* Computing Jacobians on the fly is likely to be slow for matrix-free computations because extra
* derivatives and 3x3 inverses must be calculated at every quadrature point during every mat-vec
* product.  To overcome this, we can also store the values of the Jacobian, inverse, and determinant
* at all quadrature points.  The we need only map these values into the output vectors during
* element operations. */
typedef struct {
  dReal vtx[8*3];
  dReal *jac,*jinv,jdet;
} DohpEMap_HexStored;

/* Element basis functions.  Should become private. */

/* We build these for a range of sizes and quadrature sizes.  Such a table can be compiled in or
* created at runtime depending on the size range needed. */
typedef struct {
  dScalar *basis;  // (size*qsize), basis[i*size+j] = phi_j(q_i)
  dScalar *deriv;  // (size*qsize), deriv[i*size+j] = phi_j'(q_i)
  dReal   *ncoord; // (size), nodes of Lagrange polynomial
  dInt     size;
} DohpBase;

/* At each element, we just need to store pointers to the parts of the tensor product. */
typedef struct {
  DohpBase *l;
} DohpElem_Line;

typedef struct {
  DohpBase *l[2];
} DohpElem_Quad;

typedef struct {
  DohpBase *l[3];
} DohpElem_Hex;


/* DohpDM: the distributed domain manager consists of
* - 1  DohpWF: continuum statement of the problem
* - 1  dMesh: domain and associated metadata
* - 1  dRule: quadrature rule associated with every element and every boundary facet
* - 1  DohpEMap: element coordinate mapping associated with every element with a quadrature rule
* - 1+ DohpMFS: scalar function space over a subdomain of the mesh
* - 1+ DohpField: may be scalar or vector valued, defined on a MFS
* */
EXTERN dErr DohpDMCreate(MPI_Comm,DohpDM);
EXTERN dErr DohpDMSetMesh(DohpDM,dMesh);
EXTERN dErr DohpDMGetRule(DohpDM,dRule_Hex**);
EXTERN dErr DohpDMGetMFS(DohpDM,const char[],DohpMFS*);
EXTERN dErr DohpDMCreateMFS(DohpDM,const char[],DohpMFS*);
EXTERN dErr DohpDMGetMesh(DohpDM,dMesh*);
EXTERN dErr DohpDMAddField(DohpDM,const char[],DohpMFS,dInt);
EXTERN dErr DohpDMSetUp(DohpDM);
EXTERN dErr DohpDMGetVec(DohpDM,Vec*); /* Get a global vector compatible with the parallel layout of the WF. */
EXTERN dErr DohpDMGetVecs(DohpDM,const char*[],Vec*[]);
EXTERN dErr DohpDMGetLocalVec(DohpDM,Vec*);
EXTERN dErr DohpDMGetLocalVecs(DohpDM,const char*[],Vec*[]);

EXTERN dErr DohpBlockGetMatrices(DohpBlock,const MatType,Mat*,Mat*);
EXTERN dErr DohpBlockMatMult(Mat,Vec,Vec);

/* EXTERN dErr dQuotientCreate(dQuotient); */
/* EXTERN dErr dQuotientSetMesh(dQuotient,dMesh); */
/* EXTERN dErr dQuotientSetFunction(dQuotient,dQuotientFunction1); */
/* EXTERN dErr dQuotientSetup(dQuotient); */
EXTERN dErr DohpMFSCreate(MPI_Comm,DohpMFS);
/* EXTERN dErr DohpMFSSetQuotient(DohpMFS,dQuotient); */
EXTERN dErr DohpMFSSetFunction(DohpMFS,DohpMFSFunction1);
EXTERN dErr DohpMFSSetUp(DohpMFS);
EXTERN dErr DohpMFSApplyMinimumRule(DohpMFS,const MeshListInt*);
EXTERN dErr DohpMFSSetUpElementBases(DohpMFS,const MeshListInt*);
EXTERN dErr DohpMFSSetUpElemFacetProjections(DohpMFS);

/* dQuotient: this is not actually an object, just a combination of quadrature rule and element
* map.  The element map operations need to know about the quadrature rule so we keep them together. */
EXTERN dErr dQuotientComputeElemJac_Hex(const DohpEMap_Hex*,const dRule_Hex*,dReal*,dReal*,dReal*);

PETSC_EXTERN_CXX_END
#endif /* _DOHP_H */
