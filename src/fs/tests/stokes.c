static const char help[] = "Solve non-Newtonian Stokes problem using dual order hp elements.\n"
  "The model problem is\n"
  "  -div(eta Du) + grad(p) = f\n"
  "                  div(u) = g\n"
  "where\n"
  "  D is the symmetric gradient operator\n"
  "  eta(gamma) = A (eps^2 + gamma)^{(p-2)/2}\n"
  "  gamma = Du : Du/2\n"
  "The weak form is\n"
  "  int_Omega eta Dv:Du - p div(v) - q div(u) - f_u.v - f_p.q = 0\n"
  "with Jacobian\n"
  "  int_Omega eta Dv:Du + eta' (Dv:Dw)(Dw:Du) - p div(v) - q div(u) = 0\n"
  "The problem is linear for p=2, an incompressible for g=0\n\n";

#include <dohpfs.h>
#include <dohpvec.h>
#include "petscsnes.h"

static PetscLogEvent LOG_StokesShellMult;
typedef struct _p_Stokes *Stokes;

struct StokesRheology {
  dReal A,eps,p;
};

struct StokesExactCtx {
  dReal a,b,c;
};
struct StokesExact {
  void (*solution)(const struct StokesExactCtx*,const struct StokesRheology*,const dReal x[3],dScalar u[],dScalar *p,dScalar du[],dScalar dp[]);
  void (*forcing)(const struct StokesExactCtx*,const struct StokesRheology*,const dReal x[3],dScalar fu[],dScalar *fp);
};

static void StokesExact_0_Solution(const struct StokesExactCtx *ctx,const struct StokesRheology dUNUSED *rheo,const dReal xyz[3],
                                   dScalar u[],dScalar p[],dScalar du[],dScalar dp[])
{
  const dReal dUNUSED a = ctx->a,b = ctx->b,c = ctx->c,x = xyz[0],y = xyz[1],z = xyz[2];
  u[0] = x*x*y;
  u[1] = -x*y*y;
  u[2] = 0;
  *p   = x + y - 1.0;
  /* \todo this is incorrect */
  du[0*3+0] = 0;
  du[0*3+1] = 0;
  du[0*3+2] = 0;
  du[1*3+0] = 0;
  du[1*3+1] = 0;
  du[1*3+2] = 0;
  du[2*3+0] = 0;
  du[2*3+1] = 0;
  du[2*3+2] = 0;
  dp[0]     = 0;
  dp[1]     = 0;
  dp[2]     = 0;
}
static void StokesExact_0_Forcing(const struct StokesExactCtx *ctx,const struct StokesRheology *rheo,const dReal xyz[3],dScalar fu[],dScalar *fp)
{
  const dReal dUNUSED a = ctx->a,b = ctx->b,c = ctx->c,x = xyz[0],y = xyz[1],z = xyz[2];
  fu[0] = -rheo->A*y+1;
  fu[1] = rheo->A*x+1;
  fu[2] = 0;
  *fp   = 0;
}

static void StokesExact_1_Solution(const struct StokesExactCtx *ctx,const struct StokesRheology dUNUSED *rheo,const dReal xyz[3],
                                   dScalar u[],dScalar p[],dScalar du[],dScalar dp[])
{
  const dReal dUNUSED a = ctx->a,b = ctx->b,c = ctx->c,x = xyz[0],y = xyz[1],z = xyz[2];
  u[0] = +sin(0.5*PETSC_PI*x) * cos(0.5*PETSC_PI*y);
  u[1] = -cos(0.5*PETSC_PI*x) * sin(0.5*PETSC_PI*y);
  u[2] = 0;
  *p = 0.25 * (cos(PETSC_PI*x) + cos(PETSC_PI*y)) + 10*(x+y);
  /* \todo this is incorrect */
  du[0*3+0] = 0;
  du[0*3+1] = 0;
  du[0*3+2] = 0;
  du[1*3+0] = 0;
  du[1*3+1] = 0;
  du[1*3+2] = 0;
  du[2*3+0] = 0;
  du[2*3+1] = 0;
  du[2*3+2] = 0;
  dp[0]     = 0;
  dp[1]     = 0;
  dp[2]     = 0;
}
static void StokesExact_1_Forcing(const struct StokesExactCtx *ctx,const struct StokesRheology dUNUSED *rheo,const dReal xyz[3],dScalar fu[],dScalar *fp)
{
  const dReal dUNUSED a = ctx->a,b = ctx->b,c = ctx->c,x = xyz[0],y = xyz[1],z = xyz[2];
  const dReal
    eta = 1,
    u = +sin(0.5*PETSC_PI*x) * cos(0.5*PETSC_PI*y),
    v = -cos(0.5*PETSC_PI*x) * sin(0.5*PETSC_PI*y);
  fu[0]   = dSqr(0.5 * PETSC_PI) * eta * u - 0.25 * PETSC_PI * sin(PETSC_PI * x) + 10;
  fu[1]   = dSqr(0.5 * PETSC_PI) * eta * v - 0.25 * PETSC_PI * sin(PETSC_PI * y) + 10;
  fu[2] = 0;
  *fp = 0;
}


struct StokesStore {
  dReal eta,deta;
  dReal Du[6];
};

static dErr StokesGetNullSpace(Stokes stk,MatNullSpace *matnull);
static dErr StokesShellMatMult_All_IorA(Mat A,Vec gx,Vec gy,Vec gz,InsertMode);
static dErr StokesShellMatMult_All(Mat A,Vec gx,Vec gy)
{return StokesShellMatMult_All_IorA(A,gx,gy,NULL,INSERT_VALUES);}
static dErr StokesShellMatMultAdd_All(Mat A,Vec gx,Vec gy,Vec gz)
{return StokesShellMatMult_All_IorA(A,gx,gy,gz,ADD_VALUES);}
static dErr MatMultAdd_Null(Mat dUNUSED A,Vec dUNUSED x,Vec y,Vec z)
{ return VecCopy(y,z); }
static dErr MatMult_StokesOuter_block(Mat,Vec,Vec);
static dErr MatMult_StokesOuter(Mat,Vec,Vec);
static dErr MatGetSubMatrix_StokesOuter(Mat,IS,IS,MatReuse,Mat*);
static dErr MatDestroy_StokesOuter(Mat);
static dErr MatGetVecs_Stokes(Mat,Vec*,Vec*);

/** We have two matrices of this type
*   * The high-order matrix which provides an action and offers blocks in matrix-free form
*   * The preconditioning matrix which does not provide an action, but offers assembled blocks
**/
typedef struct {
  Stokes stk;
  Mat A,Bt,B,D;
} Mat_StokesOuter;

typedef struct {
  Stokes stk;
  Mat J;
  Mat Jp;
  /* Physics-based preconditioner */
  Mat S;                        /* Schur complement in pressure space */
  KSP kspA;                     /* Solver for Au */
  KSP kspS;                     /* Solver for S */
} PC_Stokes;

struct _p_Stokes {
  MPI_Comm               comm;
  struct StokesRheology  rheo;
  struct StokesExact     exact;
  struct StokesExactCtx  exactctx;
  struct StokesStore    *store;
  dInt                  *storeoff;
  dJacobi                jac;
  dMesh                  mesh;
  dFS                    fsu,fsp;
  Vec                    xu,xp,yu,yp;
  Vec                    gvelocity,gvelocity_extra,gpressure,gpressure_extra,gpacked;
  IS                     ublock,pblock;
  VecScatter             extractVelocity,extractPressure;
  dInt                   constBDeg,pressureCodim,nominalRDeg;
  dTruth                 errorview,saddle_A_explicit,cardinalMass,neumann300;
  char                   mattype_A[256],mattype_D[256];
};

static dErr StokesCreate(MPI_Comm comm,Stokes *stokes)
{
  Stokes stk;
  dErr err;

  dFunctionBegin;
  *stokes = 0;
  err = dNew(struct _p_Stokes,&stk);dCHK(err);
  stk->comm = comm;

  stk->constBDeg     = 4;
  stk->pressureCodim = 2;
  stk->nominalRDeg   = 0;
  stk->rheo.A        = 1;
  stk->rheo.eps      = 1;
  stk->rheo.p        = 2;
  *stokes = stk;
  dFunctionReturn(0);
}

static dErr MatGetVecs_Stokes(Mat A,Vec *x,Vec *y)
{
  Stokes stk;
  dInt m,n,nu,np;
  dErr err;

  dFunctionBegin;
  err = MatShellGetContext(A,(void**)&stk);dCHK(err);
  err = MatGetLocalSize(A,&m,&n);dCHK(err);
  err = VecGetLocalSize(stk->gvelocity,&nu);dCHK(err);
  err = VecGetLocalSize(stk->gpressure,&np);dCHK(err);
  if (nu==np) dERROR(1,"Degenerate case, don't know which space to copy");
  if (x) {
    if (n == nu) {
      err = VecDuplicate(stk->gvelocity,x);dCHK(err);
    } else if (n == np) {
      err = VecDuplicate(stk->gpressure,x);dCHK(err);
    } else dERROR(1,"sizes do not agree with either space");
  }
  if (y) {
    if (n == nu) {
      err = VecDuplicate(stk->gvelocity,y);dCHK(err);
    } else if (n == np) {
      err = VecDuplicate(stk->gpressure,y);dCHK(err);
    } else dERROR(1,"sizes do not agree with either space");
  }
  dFunctionReturn(0);
}

static dErr StokesSetFromOptions(Stokes stk)
{
  struct StokesRheology *rheo = &stk->rheo;
  struct StokesExactCtx *exc = &stk->exactctx;
  dMesh mesh;
  dFS fsu,fsp;
  dJacobi jac;
  dMeshESH domain;
  dMeshTag rtag,dtag,dptag;
  dInt exact;
  dErr err;

  dFunctionBegin;
  exact = 0; exc->a = exc->b = exc->c = 1;
  err = dStrcpyS(stk->mattype_A,sizeof(stk->mattype_A),MATBAIJ);dCHK(err);
  err = dStrcpyS(stk->mattype_D,sizeof(stk->mattype_D),MATAIJ);dCHK(err);
  err = PetscOptionsBegin(stk->comm,NULL,"Stokesicity options",__FILE__);dCHK(err); {
    err = PetscOptionsInt("-const_bdeg","Use constant isotropic degree on all elements","",stk->constBDeg,&stk->constBDeg,NULL);dCHK(err);
    stk->nominalRDeg = stk->constBDeg; /* The cheapest option, usually a good default */
    err = PetscOptionsInt("-pressure_codim","Reduce pressure space by this factor","",stk->pressureCodim,&stk->pressureCodim,NULL);dCHK(err);
    err = PetscOptionsInt("-nominal_rdeg","Nominal rule degree (will be larger if basis requires it)","",stk->nominalRDeg,&stk->nominalRDeg,NULL);dCHK(err);
    err = PetscOptionsTruth("-cardinal_mass","Assemble diagonal mass matrix","",stk->cardinalMass,&stk->cardinalMass,NULL);dCHK(err);
    err = PetscOptionsTruth("-error_view","View errors","",stk->errorview,&stk->errorview,NULL);dCHK(err);
    err = PetscOptionsTruth("-saddle_A_explicit","Compute the A operator explicitly","",stk->saddle_A_explicit,&stk->saddle_A_explicit,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_A","Rate factor (rheology)","",rheo->A,&rheo->A,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_eps","Regularization (rheology)","",rheo->eps,&rheo->eps,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_p","Power p=1+1/n where n is Glen exponent","",rheo->p,&rheo->p,NULL);dCHK(err);
    err = PetscOptionsInt("-exact","Exact solution choice","",exact,&exact,NULL);dCHK(err);
    err = PetscOptionsReal("-exact_a","First scale parameter","",exc->a,&exc->a,NULL);dCHK(err);
    err = PetscOptionsReal("-exact_b","Second scale parameter","",exc->b,&exc->b,NULL);dCHK(err);
    err = PetscOptionsReal("-exact_c","Third scale parameter","",exc->c,&exc->c,NULL);dCHK(err);
    err = PetscOptionsList("-stokes_A_mat_type","Matrix type for velocity operator","",MatList,stk->mattype_A,stk->mattype_A,sizeof(stk->mattype_A),NULL);dCHK(err);
    err = PetscOptionsList("-stokes_D_mat_type","Matrix type for velocity operator","",MatList,stk->mattype_D,stk->mattype_D,sizeof(stk->mattype_D),NULL);dCHK(err);
    err = PetscOptionsTruth("-neumann300","Use boundary set 300 as Neumann conditions","",stk->neumann300,&stk->neumann300,NULL);dCHK(err);
  } err = PetscOptionsEnd();dCHK(err);

  switch (exact) {
    case 0:
      stk->exact.solution = StokesExact_0_Solution;
      stk->exact.forcing = StokesExact_0_Forcing;
      break;
    case 1:
      stk->exact.solution = StokesExact_1_Solution;
      stk->exact.forcing = StokesExact_1_Forcing;
      break;
    default: dERROR(1,"Exact solution %d not implemented");
  }

  err = dMeshCreate(stk->comm,&mesh);dCHK(err);
  err = dMeshSetInFile(mesh,"dblock.h5m",NULL);dCHK(err);
  err = dMeshSetFromOptions(mesh);dCHK(err);
  err = dMeshLoad(mesh);dCHK(err);dCHK(err);
  stk->mesh = mesh;
  err = dMeshGetRoot(mesh,&domain);dCHK(err);

  err = dJacobiCreate(stk->comm,&jac);dCHK(err);
  err = dJacobiSetDegrees(jac,9,2);dCHK(err);
  err = dJacobiSetFromOptions(jac);dCHK(err);
  err = dJacobiSetUp(jac);dCHK(err);
  stk->jac = jac;

  err = dMeshCreateRuleTagIsotropic(mesh,domain,jac,"stokes_rule_degree",stk->nominalRDeg,&rtag);dCHK(err);
  err = dMeshCreateRuleTagIsotropic(mesh,domain,jac,"stokes_efs_velocity_degree",stk->constBDeg,&dtag);dCHK(err);
  err = dMeshCreateRuleTagIsotropic(mesh,domain,jac,"stokes_efs_pressure_degree",stk->constBDeg-stk->pressureCodim,&dptag);dCHK(err);

  err = dFSCreate(stk->comm,&fsu);dCHK(err);
  err = dFSSetBlockSize(fsu,3);dCHK(err);
  err = dFSSetMesh(fsu,mesh,domain);dCHK(err);
  err = dFSSetRuleTag(fsu,jac,rtag);dCHK(err);
  err = dFSSetDegree(fsu,jac,dtag);dCHK(err);
  err = dFSRegisterBoundary(fsu,100,dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  err = dFSRegisterBoundary(fsu,200,dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  if (!stk->neumann300) {
    err = dFSRegisterBoundary(fsu,300,dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  }
  err = PetscObjectSetOptionsPrefix((dObject)fsu,"u");dCHK(err);
  err = dFSSetFromOptions(fsu);dCHK(err);
  stk->fsu = fsu;

  err = dFSCreate(stk->comm,&fsp);dCHK(err);
  err = dFSSetMesh(fsp,mesh,domain);dCHK(err);
  err = dFSSetRuleTag(fsp,jac,rtag);dCHK(err);
  err = dFSSetDegree(fsp,jac,dptag);dCHK(err);
  err = PetscObjectSetOptionsPrefix((dObject)fsp,"p");dCHK(err);
  /* No boundaries, the pressure space has Neumann conditions when Dirichlet velocity conditions are applied */
  err = dFSSetFromOptions(fsp);dCHK(err);
  stk->fsp = fsp;

  err = dFSCreateExpandedVector(fsu,&stk->xu);dCHK(err);
  err = VecDuplicate(stk->xu,&stk->yu);dCHK(err);

  err = dFSCreateExpandedVector(fsp,&stk->xp);dCHK(err);
  err = VecDuplicate(stk->xp,&stk->yp);dCHK(err);

  {                             /* Allocate space for stored values */
    dInt n,np;
    s_dRule *rule,*rulep;
    err = dFSGetElements(fsu,&n,NULL,&rule,NULL,NULL,NULL);dCHK(err);
    err = dFSGetElements(fsp,&np,NULL,&rulep,NULL,NULL,NULL);dCHK(err);
    if (n != np) dERROR(1,"pressure and velocity spaces have different number of elements");
    err = dMallocA(n+1,&stk->storeoff);dCHK(err);
    stk->storeoff[0] = 0;
    for (dInt i=0; i<n; i++) {
      dInt q,qp;
      err = dRuleGetSize(&rule[i],NULL,&q);dCHK(err);
      err = dRuleGetSize(&rulep[i],NULL,&qp);dCHK(err);
      if (q != qp) dERROR(1,"pressure and velocity spaces have different number of quadrature points on element %d",i);
      stk->storeoff[i+1] = stk->storeoff[i] + q;
    }
    err = dMallocA(stk->storeoff[n],&stk->store);dCHK(err);
    err = dMemzero(stk->store,stk->storeoff[n]*sizeof(stk->store[0]));dCHK(err);
    err = dFSRestoreElements(fsu,&n,NULL,&rule,NULL,NULL,NULL);dCHK(err);
    err = dFSRestoreElements(fsp,&np,NULL,&rulep,NULL,NULL,NULL);dCHK(err);
  }

  {
    dInt nu,np,rstart;
    IS   ublock,pblock;
    err = dFSCreateGlobalVector(stk->fsu,&stk->gvelocity);dCHK(err);
    err = VecDuplicate(stk->gvelocity,&stk->gvelocity_extra);dCHK(err);
    err = dFSCreateGlobalVector(stk->fsp,&stk->gpressure);dCHK(err);
    err = VecDuplicate(stk->gpressure,&stk->gpressure_extra);dCHK(err);
    err = VecGetLocalSize(stk->gvelocity,&nu);dCHK(err);
    err = VecGetLocalSize(stk->gpressure,&np);dCHK(err);
    err = VecCreateMPI(stk->comm,nu+np,PETSC_DETERMINE,&stk->gpacked);dCHK(err);
    err = VecGetOwnershipRange(stk->gpacked,&rstart,NULL);dCHK(err);
    err = ISCreateStride(stk->comm,nu,rstart,1,&ublock);dCHK(err);
    err = ISCreateStride(stk->comm,np,rstart+nu,1,&pblock);dCHK(err);
    err = VecScatterCreate(stk->gpacked,ublock,stk->gvelocity,NULL,&stk->extractVelocity);dCHK(err);
    err = VecScatterCreate(stk->gpacked,pblock,stk->gpressure,NULL,&stk->extractPressure);dCHK(err);
    stk->ublock = ublock;
    stk->pblock = pblock;
  }

  dFunctionReturn(0);
}

static dErr StokesDestroy(Stokes stk)
{
  dErr err;

  dFunctionBegin;
  err = dFSDestroy(stk->fsu);dCHK(err);
  err = dFSDestroy(stk->fsp);dCHK(err);
  err = dJacobiDestroy(stk->jac);dCHK(err);
  err = dMeshDestroy(stk->mesh);dCHK(err);
  err = dFree(stk->storeoff);dCHK(err);
  err = dFree(stk->store);dCHK(err);
#define _D(v)  do {if (v) {err = VecDestroy(v);dCHK(err);}} while (0)
  _D(stk->xu);
  _D(stk->yu);
  _D(stk->xp);
  _D(stk->yp);
  _D(stk->gvelocity);
  _D(stk->gpressure);
  _D(stk->gvelocity_extra);
  _D(stk->gpressure_extra);
  _D(stk->gpacked);
#undef _D
  err = VecScatterDestroy(stk->extractVelocity);dCHK(err);
  err = VecScatterDestroy(stk->extractPressure);dCHK(err);
  err = ISDestroy(stk->ublock);dCHK(err);
  err = ISDestroy(stk->pblock);dCHK(err);
  err = dFree(stk);dCHK(err);
  dFunctionReturn(0);
}

static dErr MatDestroy_StokesOuter(Mat J)
{
  Mat_StokesOuter *sms;
  dErr err;

  dFunctionBegin;
  err = MatShellGetContext(J,(void**)&sms);dCHK(err);
  if (sms->A) {err = MatDestroy(sms->A);dCHK(err);}
  if (sms->Bt) {err = MatDestroy(sms->Bt);dCHK(err);}
  if (sms->B) {err = MatDestroy(sms->B);dCHK(err);}
  if (sms->D) {err = MatDestroy(sms->D);dCHK(err);}
  err = dFree(sms);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesGetMatrices(Stokes stk,dTruth use_jblock,Mat *J,Mat *Jp)
{
  dErr err;
  dInt m,nu,np;
  Mat_StokesOuter *sms;
  Mat B,Bt;

  dFunctionBegin;
  err = VecGetLocalSize(stk->gpacked,&m);dCHK(err);
  err = VecGetLocalSize(stk->gvelocity,&nu);dCHK(err);
  err = VecGetLocalSize(stk->gpressure,&np);dCHK(err);
  err = dNew(Mat_StokesOuter,&sms);dCHK(err);
  sms->stk = stk;
  err = MatCreateShell(stk->comm,m,m,PETSC_DETERMINE,PETSC_DETERMINE,sms,J);dCHK(err);
  err = MatSetOptionsPrefix(*J,"j");dCHK(err);
  if (use_jblock) {
    err = MatShellSetOperation(*J,MATOP_MULT,(void(*)(void))MatMult_StokesOuter_block);dCHK(err);
  } else {
    err = MatShellSetOperation(*J,MATOP_MULT,(void(*)(void))MatMult_StokesOuter);dCHK(err);
  }
  err = MatShellSetOperation(*J,MATOP_GET_SUBMATRIX,(void(*)(void))MatGetSubMatrix_StokesOuter);dCHK(err);
  err = MatShellSetOperation(*J,MATOP_DESTROY,(void(*)(void))MatDestroy_StokesOuter);dCHK(err);
  err = MatSetFromOptions(*J);dCHK(err);

  /* Create high-order matrix for diagonal velocity block, with context \a stk */
  err = MatCreateShell(stk->comm,nu,nu,PETSC_DETERMINE,PETSC_DETERMINE,stk,&sms->A);dCHK(err);
  err = MatShellSetOperation(sms->A,MATOP_GET_VECS,(void(*)(void))MatGetVecs_Stokes);dCHK(err);
  err = MatShellSetOperation(sms->A,MATOP_MULT,(void(*)(void))StokesShellMatMult_All);dCHK(err);
  err = MatShellSetOperation(sms->A,MATOP_MULT_TRANSPOSE,(void(*)(void))StokesShellMatMult_All);dCHK(err);
  err = MatShellSetOperation(sms->A,MATOP_MULT_ADD,(void(*)(void))StokesShellMatMultAdd_All);dCHK(err);
  err = MatShellSetOperation(sms->A,MATOP_MULT_TRANSPOSE_ADD,(void(*)(void))StokesShellMatMultAdd_All);dCHK(err);

  /* Create off-diagonal high-order matrix, with context \a stk */
  err = MatCreateShell(stk->comm,np,nu,PETSC_DETERMINE,PETSC_DETERMINE,stk,&B);dCHK(err);
  err = MatShellSetOperation(B,MATOP_GET_VECS,(void(*)(void))MatGetVecs_Stokes);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT,(void(*)(void))StokesShellMatMult_All);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT_TRANSPOSE,(void(*)(void))StokesShellMatMult_All);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT_ADD,(void(*)(void))StokesShellMatMultAdd_All);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT_TRANSPOSE_ADD,(void(*)(void))StokesShellMatMultAdd_All);dCHK(err);
  err = MatCreateTranspose(B,&Bt);dCHK(err);
  sms->B  = B;
  sms->Bt = Bt;
  err = MatCreateShell(stk->comm,np,np,PETSC_DETERMINE,PETSC_DETERMINE,NULL,&sms->D);dCHK(err);
  err = MatShellSetOperation(sms->D,MATOP_MULT_ADD,(void(*)(void))MatMultAdd_Null);dCHK(err);
  sms = NULL;                   /* J takes ownership of \a sms */

  /* Create preconditioning wrapper, does not implement MatMult. */
  err = dNew(Mat_StokesOuter,&sms);dCHK(err);
  sms->stk = stk;
  err = MatCreateShell(stk->comm,m,m,PETSC_DETERMINE,PETSC_DETERMINE,sms,Jp);dCHK(err);
  err = MatSetOptionsPrefix(*Jp,"jp");dCHK(err);
  err = MatShellSetOperation(*Jp,MATOP_GET_SUBMATRIX,(void(*)(void))MatGetSubMatrix_StokesOuter);dCHK(err);
  err = MatShellSetOperation(*Jp,MATOP_DESTROY,(void(*)(void))MatDestroy_StokesOuter);dCHK(err);
  err = MatSetFromOptions(*Jp);dCHK(err);

  /* Create real matrix to be used for preconditioning */
  err = dFSGetMatrix(stk->fsu,stk->mattype_A,&sms->A);dCHK(err);
  err = dFSGetMatrix(stk->fsp,stk->mattype_D,&sms->D);dCHK(err);
  err = MatSetOptionsPrefix(sms->A,"stokes_A_");dCHK(err);
  err = MatSetOptionsPrefix(sms->D,"stokes_D_");dCHK(err);

  {                             /* Allocate for the pressure Poisson, used by PCLSC */
    Mat L;
    Vec Mdiag;
    err = dFSGetMatrix(stk->fsp,stk->mattype_D,&L);dCHK(err);
    err = MatSetOptionsPrefix(L,"stokes_L_");dCHK(err);
    err = MatSetFromOptions(L);dCHK(err);
    err = PetscObjectCompose((dObject)sms->D,"LSC_L",(dObject)L);dCHK(err);
    err = PetscObjectCompose((dObject)sms->D,"LSC_Lp",(dObject)L);dCHK(err);
    err = MatDestroy(L);dCHK(err); /* don't keep a reference */
    err = VecDuplicate(stk->gvelocity,&Mdiag);dCHK(err);
    err = PetscObjectCompose((dObject)sms->D,"LSC_M_diag",(dObject)Mdiag);
    err = VecDestroy(Mdiag);dCHK(err); /* don't keep a reference */
  }

  /* This chunk is not normally needed.  We set it just so that MatMult can be implemented, mainly for debugging purposes */
  err = MatShellSetOperation(*Jp,MATOP_MULT,(void(*)(void))MatMult_StokesOuter_block);dCHK(err);
  err = PetscObjectReference((dObject)B);dCHK(err);
  err = PetscObjectReference((dObject)Bt);dCHK(err);
  sms->B  = B;
  sms->Bt = Bt;

  sms = NULL;                   /* Jp takes ownership of \a sms */
  dFunctionReturn(0);
}

static inline void StokesPointwiseComputeStore(struct StokesRheology *rheo,const dReal dUNUSED x[3],const dScalar Du[],struct StokesStore *st)
{
  dScalar gamma_reg = dSqr(rheo->eps) + dColonSymScalar3(Du,Du);
  st->eta = rheo->A * pow(gamma_reg,0.5*(rheo->p-2));
  st->deta = 0.5*(rheo->p-2) * st->eta / gamma_reg;
  for (dInt i=0; i<6; i++) st->Du[i] = Du[i];
}

static inline void StokesPointwiseFunction(struct StokesRheology *rheo,struct StokesExact *exact,struct StokesExactCtx *exactctx,
                                           const dReal x[3],dReal weight,const dScalar Du[6],dScalar p,
                                           struct StokesStore *st,dScalar v[3],dScalar Dv[6],dScalar *q)
{
  dScalar fu[3],fp;
  StokesPointwiseComputeStore(rheo,x,Du,st);
  exact->forcing(exactctx,rheo,x,fu,&fp);
  for (dInt i=0; i<3; i++) v[i] = -weight * fu[i]; /* Coefficient of \a v in weak form, only appears in forcing term */
  *q   = -weight * (Du[0]+Du[1]+Du[2] + fp);       /* -q tr(Du) - forcing, note tr(Du) = div(u) */
  for (dInt i=0; i<3; i++) Dv[i] = weight * (st->eta * Du[i] - p); /* eta Dv:Du - p tr(Dv) */
  for (dInt i=3; i<6; i++) Dv[i] = weight * st->eta * Du[i];       /* eta Dv:Du */
}

static inline void StokesPointwiseJacobian(const struct StokesStore *restrict st,dReal weight,
                                           const dScalar Du[restrict static 6],dScalar p,
                                           dScalar Dv[restrict static 6],dScalar *restrict q)
{
                                /* Coefficients in weak form of Jacobian */
  const dScalar deta_colon = st->deta*dColonSymScalar3(st->Du,Du);                          /* eta' Dw:Du */
  for (dInt i=0; i<3; i++) Dv[i] = weight * (st->eta * Du[i] + deta_colon * st->Du[i] - p); /* eta Dv:Du + eta' (Dv:Dw)(Dw:Du) - p tr(Dv) */
  for (dInt i=3; i<6; i++) Dv[i] = weight * (st->eta * Du[i] + deta_colon * st->Du[i]);     /* eta Dv:Du + eta' (Dv:Dw)(Dw:Du) */
  *q = -weight*(Du[0]+Du[1]+Du[2]);                                                         /* -q tr(Du) */
}

static inline void StokesPointwiseJacobian_A(const struct StokesStore *restrict st,dReal weight,const dScalar Du[restrict static 6],dScalar Dv[restrict static 6])
{
  const dScalar deta_colon = st->deta*dColonSymScalar3(st->Du,Du);
  for (dInt i=0; i<6; i++) Dv[i] = weight * (st->eta*Du[i] + deta_colon*st->Du[i]);
}

static inline void StokesPointwiseJacobian_B(dReal weight,const dScalar Du[restrict static 6],dScalar *restrict q)
{
  *q = -weight*(Du[0]+Du[1]+Du[2]);
}

static inline void StokesPointwiseJacobian_Bt(dReal weight,dScalar p,dScalar Dv[restrict static 6])
{
  for (dInt i=0; i<3; i++) Dv[i] = -weight*p;
  for (dInt i=3; i<6; i++) Dv[i] = 0;
}

static dErr StokesFunction(SNES dUNUSED snes,Vec gx,Vec gy,void *ctx)
{
  dReal (*restrict geom)[3],(*restrict q)[3],(*restrict jinv)[3][3],*restrict jw;
  Stokes   stk = ctx;
  dFS      fsu = stk->fsu,fsp = stk->fsp;
  Vec      gxu,gxp;
  dInt     n,np,*off,*offp,*geomoff;
  s_dRule *rule,*rulep;
  s_dEFS  *efs,*efsp;
  dScalar *xu,*xp,*yu,*yp,*restrict vv,*restrict du,*restrict dv,*restrict pp,*restrict qq;
  dErr    err;

  dFunctionBegin;
  gxu = stk->gvelocity; gxp = stk->gpressure; /* Our work vectors */
  err = VecScatterBegin(stk->extractVelocity,gx,gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,gx,gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,gx,gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,gx,gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  /* dFS_INHOMOGENEOUS projects into inhomogeneous space (strongly enforcing boundary conditions) */
  err = dFSGlobalToExpanded(fsu,gxu,stk->xu,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err); /* velocity */
  err = dFSGlobalToExpanded(fsp,gxp,stk->xp,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err); /* pressure */
  err = VecGetArray(stk->xu,&xu);dCHK(err);
  err = VecGetArray(stk->xp,&xp);dCHK(err);
  err = VecGetArray(stk->yu,&yu);dCHK(err);
  err = VecGetArray(stk->yp,&yp);dCHK(err);
  err = dFSGetElements(fsu,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err); /* \note \a off is in terms of \e nodes, not \e dofs */
  err = dFSGetElements(fsp,&np,&offp,&rulep,&efsp,NULL,NULL);dCHK(err);
  if (n != np) dERROR(1,"number of elements in velocity and pressure spaces do not agree");
  err = dFSGetWorkspace(fsu,__func__,&q,&jinv,&jw,NULL,&vv,&du,&dv);dCHK(err);
  err = dFSGetWorkspace(fsp,__func__,NULL,NULL,NULL,&pp,&qq,NULL,NULL);dCHK(err); /* workspace for test and trial functions */
  for (dInt e=0; e<n; e++) {
    dInt Q;
    err = dRuleComputeGeometry(&rule[e],(const dReal(*)[3])(geom+geomoff[e]),q,jinv,jw);dCHK(err);
    err = dRuleGetSize(&rule[e],0,&Q);dCHK(err);
    {
      dInt Qp;
      err = dRuleGetSize(&rulep[e],0,&Qp);dCHK(err);
      if (Q != Qp) dERROR(1,"rule sizes on element %d do not agree",e);dCHK(err);
    }
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,xu+3*off[e],du,dAPPLY_GRAD,INSERT_VALUES);dCHK(err); /* velocity gradients */
    err = dEFSApply(&efsp[e],(const dReal*)jinv,1,xp+offp[e],pp,dAPPLY_INTERP,INSERT_VALUES);dCHK(err); /* pressure values */
    for (dInt i=0; i<Q; i++) {
      struct StokesStore *restrict st = &stk->store[stk->storeoff[e]+i];
      dScalar Du[6],Dv[6];
      dTensorSymCompress3(&du[i*9],Du);
      StokesPointwiseFunction(&stk->rheo,&stk->exact,&stk->exactctx,q[i],jw[i],Du,pp[i],st,&vv[i*3],Dv,&qq[i]);
      dTensorSymUncompress3(Dv,&dv[i*9]);
    }
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,vv,yu+3*off[e],dAPPLY_INTERP_TRANSPOSE,INSERT_VALUES);dCHK(err);
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,dv,yu+3*off[e],dAPPLY_GRAD_TRANSPOSE,ADD_VALUES);dCHK(err);
    err = dEFSApply(&efsp[e],(const dReal*)jinv,1,qq,yp+offp[e],dAPPLY_INTERP_TRANSPOSE,INSERT_VALUES);dCHK(err);
  }
  err = dFSRestoreWorkspace(fsu,__func__,&q,&jinv,&jw,NULL,&vv,&du,&dv);dCHK(err);
  err = dFSRestoreWorkspace(fsp,__func__,NULL,NULL,NULL,&pp,&qq,NULL,NULL);dCHK(err);
  err = dFSRestoreElements(fsu,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSRestoreElements(fsp,&np,&offp,&rulep,&efsp,NULL,NULL);dCHK(err);
  err = VecRestoreArray(stk->xu,&xu);dCHK(err);
  err = VecRestoreArray(stk->xp,&xp);dCHK(err);
  err = VecRestoreArray(stk->yu,&yu);dCHK(err);
  err = VecRestoreArray(stk->yp,&yp);dCHK(err);
  {
    err = VecZeroEntries(gxu);dCHK(err);
    err = VecZeroEntries(gxp);dCHK(err);
    err = dFSExpandedToGlobal(fsu,stk->yu,gxu,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err);
    err = dFSExpandedToGlobal(fsp,stk->yp,gxp,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err);
    err = VecScatterBegin(stk->extractVelocity,gxu,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
    err = VecScatterEnd  (stk->extractVelocity,gxu,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
    err = VecScatterBegin(stk->extractPressure,gxp,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
    err = VecScatterEnd  (stk->extractPressure,gxp,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  }
  dFunctionReturn(0);
}

static dErr MatGetSubMatrix_StokesOuter(Mat J,IS rows,IS cols,MatReuse reuse,Mat *submat)
{
  dErr err;
  Mat_StokesOuter *sms;

  dFunctionBegin;
  err = MatShellGetContext(J,(void**)&sms);dCHK(err);
  for (dInt i=0; i<4; i++) {
    dTruth match;
    IS trows,tcols;
    Mat tmat;
    switch (i) {
      case 0:
        trows = sms->stk->ublock;
        tcols = sms->stk->ublock;
        tmat  = sms->A;
        break;
      case 1:
        trows = sms->stk->ublock;
        tcols = sms->stk->pblock;
        tmat  = sms->Bt;
        break;
      case 2:
        trows = sms->stk->pblock;
        tcols = sms->stk->ublock;
        tmat  = sms->B;
        break;
      case 3:
        trows = sms->stk->pblock;
        tcols = sms->stk->pblock;
        tmat  = sms->D;
        break;
      default: continue;
    }
    err = ISEqual(rows,trows,&match);dCHK(err);
    if (!match) continue;
    err = ISEqual(cols,tcols,&match);dCHK(err);
    if (!match) continue;
    if (reuse == MAT_INITIAL_MATRIX) {err = PetscObjectReference((dObject)tmat);dCHK(err);}
    *submat = tmat;
    dFunctionReturn(0);
  }
  dERROR(1,"Submatrix not available");
  dFunctionReturn(0);
}

static dErr MatMult_StokesOuter(Mat J,Vec gx,Vec gy)
{
  Mat_StokesOuter *sms;
  dReal (*restrict geom)[3],(*restrict q)[3],(*restrict jinv)[3][3],*restrict jw;
  Stokes   stk;
  dFS      fsu,fsp;
  Vec      gxu,gxp;
  dInt     n,np,*off,*offp,*geomoff;
  s_dRule *rule,*rulep;
  s_dEFS  *efs,*efsp;
  dScalar *xu,*xp,*yu,*yp,*restrict vv,*restrict du,*restrict dv,*restrict pp,*restrict qq;
  dErr    err;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_StokesShellMult,J,gx,gy,0);dCHK(err);
  err = MatShellGetContext(J,(void**)&sms);dCHK(err);
  stk = sms->stk;
  fsu = stk->fsu; fsp = stk->fsp;
  gxu = stk->gvelocity; gxp = stk->gpressure; /* Our work vectors */
  err = VecScatterBegin(stk->extractVelocity,gx,gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,gx,gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,gx,gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,gx,gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  /* dFS_HOMOGENEOUS projects into homogeneous space (because Dirichlet conditions are enforced strongly) */
  err = dFSGlobalToExpanded(fsu,gxu,stk->xu,dFS_HOMOGENEOUS,INSERT_VALUES);dCHK(err); /* velocity */
  err = dFSGlobalToExpanded(fsp,gxp,stk->xp,dFS_HOMOGENEOUS,INSERT_VALUES);dCHK(err); /* pressure */
  err = VecGetArray(stk->xu,&xu);dCHK(err);
  err = VecGetArray(stk->xp,&xp);dCHK(err);
  err = VecGetArray(stk->yu,&yu);dCHK(err);
  err = VecGetArray(stk->yp,&yp);dCHK(err);
  err = dFSGetElements(fsu,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err); /* \note \a off is in terms of \e nodes, not \e dofs */
  err = dFSGetElements(fsp,&np,&offp,&rulep,&efsp,NULL,NULL);dCHK(err);
  if (n != np) dERROR(1,"number of elements in velocity and pressure spaces do not agree");
  err = dFSGetWorkspace(fsu,__func__,&q,&jinv,&jw,NULL,&vv,&du,&dv);dCHK(err);
  err = dFSGetWorkspace(fsp,__func__,NULL,NULL,NULL,&pp,&qq,NULL,NULL);dCHK(err); /* workspace for test and trial functions */
  for (dInt e=0; e<n; e++) {
    dInt Q;
    err = dRuleComputeGeometry(&rule[e],(const dReal(*)[3])(geom+geomoff[e]),q,jinv,jw);dCHK(err);
    err = dRuleGetSize(&rule[e],0,&Q);dCHK(err);
    {
      dInt Qp;
      err = dRuleGetSize(&rulep[e],0,&Qp);dCHK(err);
      if (Q != Qp) dERROR(1,"rule sizes on element %d do not agree",e);dCHK(err);
    }
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,xu+3*off[e],du,dAPPLY_GRAD,INSERT_VALUES);dCHK(err); /* velocity gradients */
    err = dEFSApply(&efsp[e],(const dReal*)jinv,1,xp+offp[e],pp,dAPPLY_INTERP,INSERT_VALUES);dCHK(err); /* pressure values */
    for (dInt i=0; i<Q; i++) {
      struct StokesStore *restrict st = &stk->store[stk->storeoff[e]+i];
      dScalar Du[6],Dv[6];
      dTensorSymCompress3(&du[i*9],Du);
      StokesPointwiseJacobian(st,jw[i],Du,pp[i],Dv,&qq[i]);
      dTensorSymUncompress3(Dv,&dv[i*9]);
    }
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,dv,yu+3*off[e],dAPPLY_GRAD_TRANSPOSE,INSERT_VALUES);dCHK(err);
    err = dEFSApply(&efsp[e],(const dReal*)jinv,1,qq,yp+offp[e],dAPPLY_INTERP_TRANSPOSE,INSERT_VALUES);dCHK(err);
  }
  err = dFSRestoreWorkspace(fsu,__func__,&q,&jinv,&jw,NULL,&vv,&du,&dv);dCHK(err);
  err = dFSRestoreWorkspace(fsp,__func__,NULL,NULL,NULL,&pp,&qq,NULL,NULL);dCHK(err);
  err = dFSRestoreElements(fsu,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSRestoreElements(fsp,&np,&offp,&rulep,&efsp,NULL,NULL);dCHK(err);
  err = VecRestoreArray(stk->xu,&xu);dCHK(err);
  err = VecRestoreArray(stk->xp,&xp);dCHK(err);
  err = VecRestoreArray(stk->yu,&yu);dCHK(err);
  err = VecRestoreArray(stk->yp,&yp);dCHK(err);
  {
    err = VecZeroEntries(gxu);dCHK(err);
    err = VecZeroEntries(gxp);dCHK(err);
    err = dFSExpandedToGlobal(fsu,stk->yu,gxu,dFS_HOMOGENEOUS,INSERT_VALUES);dCHK(err);
    err = dFSExpandedToGlobal(fsp,stk->yp,gxp,dFS_HOMOGENEOUS,INSERT_VALUES);dCHK(err);
    err = VecScatterBegin(stk->extractVelocity,gxu,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
    err = VecScatterEnd  (stk->extractVelocity,gxu,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
    err = VecScatterBegin(stk->extractPressure,gxp,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
    err = VecScatterEnd  (stk->extractPressure,gxp,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  }
  err = PetscLogEventEnd(LOG_StokesShellMult,J,gx,gy,0);dCHK(err);
  dFunctionReturn(0);
}

static dErr MatMult_StokesOuter_block(Mat J,Vec gx,Vec gy)
{
  Mat_StokesOuter *sms;
  Stokes stk;
  Vec    gxu,gxp,gyu;
  dErr   err;

  dFunctionBegin;
  err = MatShellGetContext(J,(void**)&sms);dCHK(err);
  stk = sms->stk;
  gxu = stk->gvelocity; gxp = stk->gpressure;
  gyu = stk->gvelocity_extra;
  err = VecScatterBegin(stk->extractVelocity,gx,gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,gx,gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = MatMult(sms->B,gxu,gxp);dCHK(err); /* Use pressure vector for output of p = B u */
  err = VecScatterBegin(stk->extractPressure,gxp,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,gxp,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);

  err = MatMult(sms->A,gxu,gyu);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,gx,gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,gx,gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = MatMultTransposeAdd(sms->B,gxp,gyu,gyu);dCHK(err);
  err = VecScatterBegin(stk->extractVelocity,gyu,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,gyu,gy,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  dFunctionReturn(0);
}

typedef enum {STOKES_MULT_A,STOKES_MULT_Bt,STOKES_MULT_B} StokesMultMode;

static dErr dUNUSED StokesShellMatMult_All_IorA(Mat A,Vec gx,Vec gy,Vec gz,InsertMode imode)
{
  Stokes          stk;
  dFS             fsx,fsy,fslarger;
  dInt            n,*off,*offy,*geomoff;
  s_dRule        *rule;
  s_dEFS         *efs,*efsy;
  Vec             X,Y;
  dScalar        *x,*y,*restrict uu,*restrict vv,*restrict du,*restrict dv;
  StokesMultMode  mmode;
  dReal (*restrict geom)[3],(*restrict q)[3],(*restrict jinv)[3][3],*restrict jw;
  dErr err;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_StokesShellMult,A,gx,gy,gz);dCHK(err);
  err = MatShellGetContext(A,(void**)&stk);dCHK(err);
  {  /* Find out which block we have by comparing sizes */
    dInt nu,np,nx,ny;
    err = VecGetSize(stk->gvelocity,&nu);dCHK(err);
    err = VecGetSize(stk->gpressure,&np);dCHK(err);
    err = VecGetSize(gx,&nx);dCHK(err);
    err = VecGetSize(gy,&ny);dCHK(err);
    if (nx==nu && ny==nu) mmode = STOKES_MULT_A;
    else if (nx==np && ny==nu) mmode = STOKES_MULT_Bt;
    else if (nx==nu && ny==np) mmode = STOKES_MULT_B;
    else dERROR(1,"Sizes do not match, unknown mult operation");
  }
  switch (mmode) {
    case STOKES_MULT_A:  fsx = fsy = stk->fsu; X = stk->xu; Y = stk->yu; break;
    case STOKES_MULT_Bt: fsx = stk->fsp; fsy = stk->fsu; X = stk->xp; Y = stk->yu; break;
    case STOKES_MULT_B:  fsx = stk->fsu; fsy = stk->fsp; X = stk->xu; Y = stk->yp; break;
    default: dERROR(1,"should not happen");
  }
  err = dFSGlobalToExpanded(fsx,gx,X,dFS_HOMOGENEOUS,INSERT_VALUES);dCHK(err);
  err = VecGetArray(X,&x);dCHK(err);
  err = VecGetArray(Y,&y);dCHK(err);
  err = dFSGetElements(fsx,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSGetElements(fsy,NULL,&offy,NULL,&efsy,NULL,NULL);dCHK(err);
  fslarger = off[n]>offy[n] ? fsx : fsy;
  err = dFSGetWorkspace(fslarger,__func__,&q,&jinv,&jw,&uu,&vv,&du,&dv);dCHK(err);
  for (dInt e=0; e<n; e++) {
    dInt Q;
    err = dRuleComputeGeometry(&rule[e],(const dReal(*)[3])(geom+geomoff[e]),q,jinv,jw);dCHK(err);
    err = dRuleGetSize(&rule[e],0,&Q);dCHK(err);
    switch (mmode) {
      case STOKES_MULT_A:
        err = dEFSApply(&efs[e],(const dReal*)jinv,3,x+3*off[e],du,dAPPLY_GRAD,INSERT_VALUES);dCHK(err);
        for (dInt i=0; i<Q; i++) {
          struct StokesStore *restrict st = &stk->store[stk->storeoff[e]+i];
          dScalar Du[6],Dv[6],qq_unused[1];
          dTensorSymCompress3(&du[i*9],Du);
          StokesPointwiseJacobian(st,jw[i],Du,0,Dv,qq_unused);
          //StokesPointwiseJacobian_A(st,jw[i],Du,Dv);
          dTensorSymUncompress3(Dv,&dv[i*9]);
        }
        err = dEFSApply(&efsy[e],(const dReal*)jinv,3,dv,y+3*offy[e],dAPPLY_GRAD_TRANSPOSE,INSERT_VALUES);dCHK(err);
        break;
      case STOKES_MULT_Bt:
        err = dEFSApply(&efs[e],(const dReal*)jinv,1,x+off[e],uu,dAPPLY_INTERP,INSERT_VALUES);dCHK(err); /* pressure values */
        for (dInt i=0; i<Q; i++) {
          dScalar Dv[6];
          StokesPointwiseJacobian_Bt(jw[i],uu[i],Dv);
          dTensorSymUncompress3(Dv,&dv[i*9]);
        }
        err = dEFSApply(&efsy[e],(const dReal*)jinv,3,dv,y+3*offy[e],dAPPLY_GRAD_TRANSPOSE,INSERT_VALUES);dCHK(err);
        break;
      case STOKES_MULT_B:
        err = dEFSApply(&efs[e],(const dReal*)jinv,3,x+3*off[e],du,dAPPLY_GRAD,INSERT_VALUES);dCHK(err);
        for (dInt i=0; i<Q; i++) {
          dScalar Du[6];
          dTensorSymCompress3(&du[i*9],Du);
          StokesPointwiseJacobian_B(jw[i],Du,&vv[i]); /* vv is pressure test function */
        }
        err = dEFSApply(&efsy[e],(const dReal*)jinv,1,vv,y+offy[e],dAPPLY_INTERP_TRANSPOSE,INSERT_VALUES);dCHK(err);
        break;
    }
  }
  err = dFSRestoreWorkspace(fslarger,__func__,&q,&jinv,&jw,&uu,&vv,&du,&dv);dCHK(err);
  err = dFSRestoreElements(fsx,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSRestoreElements(fsy,NULL,&offy,NULL,&efsy,NULL,NULL);dCHK(err);
  err = VecRestoreArray(X,&x);dCHK(err);
  err = VecRestoreArray(Y,&y);dCHK(err);
  switch (imode) {
    case INSERT_VALUES:
      if (gz) dERROR(1,"Cannot use INSERT_VALUES and set gz");
      gz = gy;
      err = VecZeroEntries(gz);dCHK(err);
      break;
    case ADD_VALUES:
      if (gz != gy) {
        err = VecCopy(gy,gz);dCHK(err);
      }
      break;
    default: dERROR(1,"unsupported imode");
  }
  err = dFSExpandedToGlobal(fsy,Y,gz,dFS_HOMOGENEOUS,imode);dCHK(err);
  err = PetscLogEventEnd(LOG_StokesShellMult,A,gx,gy,gz);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesJacobianAssemble_Velocity(Stokes stk,Mat Jp,Vec Mdiag,Vec gx)
{
  Mat_StokesOuter *sms;
  s_dRule *rule;
  s_dEFS *efs;
  dReal (*nx)[3];
  dScalar *x,*mdiag;
  dFS fs = stk->fsu;
  dInt n,*off,*geomoff;
  dReal (*geom)[3];
  dErr err;

  dFunctionBegin;
  err = MatShellGetContext(Jp,(void**)&sms);dCHK(err);
  err = MatZeroEntries(sms->A);dCHK(err);
  err = VecScatterBegin(stk->extractVelocity,gx,stk->gvelocity,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,gx,stk->gvelocity,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = dFSGlobalToExpanded(fs,stk->gvelocity,stk->xu,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err);
  err = VecGetArray(stk->xu,&x);dCHK(err);
  err = VecGetArray(stk->yu,&mdiag);dCHK(err);
  err = dFSGetElements(fs,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSGetWorkspace(fs,__func__,&nx,NULL,NULL,NULL,NULL,NULL,NULL);dCHK(err); /* We only need space for nodal coordinates */
  for (dInt e=0; e<n; e++) {
    dInt three,P[3];
    dReal *nweight[3];
    err = dEFSGetGlobalCoordinates(&efs[e],(const dReal(*)[3])(geom+geomoff[e]),&three,P,nx);dCHK(err);
    err = dEFSGetTensorNodes(&efs[e],NULL,NULL,NULL,nweight,NULL,NULL);dCHK(err);
    if (three != 3) dERROR(1,"Dimension not equal to 3");
    for (dInt i=0; i<P[0]-1; i++) { /* P-1 = number of sub-elements in each direction */
      for (dInt j=0; j<P[1]-1; j++) {
        for (dInt k=0; k<P[2]-1; k++) {
          dQ1CORNER_CONST_DECLARE(c,rowcol,corners,off[e],nx,P,i,j,k);
          const dScalar (*uc)[3] = (const dScalar(*)[3])x+off[e]; /* function values, indexed at subelement corners \c uc[c[#]][0] */
          const dReal (*qx)[3],*jw,(*basis)[8],(*deriv)[8][3];
          dInt qn;
          dScalar K[8*3][8*3];
          err = dMemzero(K,sizeof(K));dCHK(err);
          err = dQ1HexComputeQuadrature(corners,&qn,&qx,&jw,(const dReal**)&basis,(const dReal**)&deriv);dCHK(err);
          for (dInt lq=0; lq<qn; lq++) { /* loop over quadrature points */
            struct StokesStore st;
            { /* Set up store */
              dReal st_Du[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
              for (dInt lp=0; lp<8; lp++) { /* Evaluate function values and gradients at this quadrature point */
                for (dInt f=0; f<3; f++) {  /* for each field */
                  st_Du[f][0] += deriv[lq][lp][0] * uc[c[lp]][f];
                  st_Du[f][1] += deriv[lq][lp][1] * uc[c[lp]][f];
                  st_Du[f][2] += deriv[lq][lp][2] * uc[c[lp]][f];
                }
              }
              StokesPointwiseComputeStore(&stk->rheo,qx[lq],&st_Du[0][0],&st);
            }
            for (dInt ltest=0; ltest<8; ltest++) {              /* Loop over test basis functions (corners) */
              for (dInt lp=0; lp<8; lp++) {                     /* Loop over trial basis functions (corners) */
                for (dInt fp=0; fp<3; fp++) {                   /* Each field component of trial function */
                  dScalar Du[3][3] = {{0,0,0},{0,0,0},{0,0,0}},Dv[3][3],Dusym[6],Dvsym[6],q_unused;
                  Du[fp][0] = deriv[lq][lp][0]; /* Trial function for only this field component */
                  Du[fp][1] = deriv[lq][lp][1];
                  Du[fp][2] = deriv[lq][lp][2];
                  /* Get the coefficients of test functions for each field component */
                  dTensorSymCompress3(&Du[0][0],Dusym);
                  StokesPointwiseJacobian(&st,jw[lq],Dusym,0,Dvsym,&q_unused);
                  dTensorSymUncompress3(Dvsym,&Dv[0][0]);
                  for (dInt ftest=0; ftest<3; ftest++) { /* Insert contribution from each test function field component */
                    K[ltest*3+ftest][lp*3+fp] += //basis[lq][ltest] * v[0]
                      + deriv[lq][ltest][0] * Dv[ftest][0]
                      + deriv[lq][ltest][1] * Dv[ftest][1]
                      + deriv[lq][ltest][2] * Dv[ftest][2];
                  }
                }
              }
            }
          }
          err = dFSMatSetValuesBlockedExpanded(fs,sms->A,8,rowcol,8,rowcol,&K[0][0],ADD_VALUES);dCHK(err);
          for (dInt f=0; f<3; f++) {
            mdiag[rowcol[0]*3+f] = nweight[0][i+0]*nweight[1][j+0]*nweight[2][k+0];
            mdiag[rowcol[1]*3+f] = nweight[0][i+1]*nweight[1][j+0]*nweight[2][k+0];
            mdiag[rowcol[2]*3+f] = nweight[0][i+1]*nweight[1][j+1]*nweight[2][k+0];
            mdiag[rowcol[3]*3+f] = nweight[0][i+0]*nweight[1][j+1]*nweight[2][k+0];
            mdiag[rowcol[4]*3+f] = nweight[0][i+0]*nweight[1][j+0]*nweight[2][k+1];
            mdiag[rowcol[5]*3+f] = nweight[0][i+1]*nweight[1][j+0]*nweight[2][k+1];
            mdiag[rowcol[6]*3+f] = nweight[0][i+1]*nweight[1][j+1]*nweight[2][k+1];
            mdiag[rowcol[7]*3+f] = nweight[0][i+0]*nweight[1][j+1]*nweight[2][k+1];
          }
        }
      }
    }
  }
  err = dFSRestoreWorkspace(fs,__func__,&nx,NULL,NULL,NULL,NULL,NULL,NULL);dCHK(err);
  err = dFSRestoreElements(fs,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = VecRestoreArray(stk->xu,&x);dCHK(err);
  err = VecRestoreArray(stk->yu,&mdiag);dCHK(err);

  /* \bug in parallel: We need the ghost update to be INSERT_VALUES, duplicates should be identical. */
  err = dFSExpandedToGlobal(fs,stk->yu,Mdiag,dFS_HOMOGENEOUS,INSERT_VALUES);dCHK(err);

  err = MatAssemblyBegin(sms->A,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyEnd(sms->A,MAT_FINAL_ASSEMBLY);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesJacobianAssemble_Pressure(Stokes stk,Mat D,Mat Daux,Vec gx)
{
  s_dRule         *rule;
  s_dEFS          *efs;
  dReal            (*nx)[3];
  dScalar         *x;
  dFS              fsu = stk->fsu,fsp = stk->fsp;
  dInt             n,*off,*geomoff;
  dReal            (*geom)[3];
  dErr             err;

  dFunctionBegin;
  err = MatZeroEntries(D);dCHK(err);
  if (Daux) {err = MatZeroEntries(Daux);dCHK(err);}
  /* It might seem weird to be getting velocity in the pressure assembly.  The reason is that this preconditioner
  * (indeed the entire problem) is always linear in pressure.  It \e might be nonlinear in velocity. */
  err = VecScatterBegin(stk->extractVelocity,gx,stk->gvelocity,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,gx,stk->gvelocity,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = dFSGlobalToExpanded(fsu,stk->gvelocity,stk->xu,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err);
  err = VecGetArray(stk->xu,&x);dCHK(err);
  err = dFSGetElements(fsp,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSGetWorkspace(fsp,__func__,&nx,NULL,NULL,NULL,NULL,NULL,NULL);dCHK(err); /* We only need space for nodal coordinates */
  for (dInt e=0; e<n; e++) {
    dInt three,P[3];
#if 0
    const dScalar (*uc)[3] = (const dScalar(*)[3])x+off[e]; /* function values, indexed at subelement corners \c uc[c[#]][0] */
#endif
    if (stk->cardinalMass) {
      dReal *weight[3];
      err = dEFSGetTensorNodes(&efs[e],&three,P,NULL,weight,NULL,NULL);dCHK(err);
      if (three!= 3) dERROR(1,"Dimension not equal to 3");
      for (dInt i=0; i<P[0]; i++) {
        for (dInt j=0; j<P[1]; j++) {
          for (dInt k=0; k<P[2]; k++) {
            dInt rowcol = off[e] + (i*P[1]+j)*P[2]+k;
            dScalar v = weight[0][i]*weight[1][j]*weight[2][k];
            /* Should be scaled by local Jacobian, doesn't matter due to uniform mesh */
            err = dFSMatSetValuesBlockedExpanded(fsp,D,1,&rowcol,1,&rowcol,&v,INSERT_VALUES);dCHK(err);
          }
        }
      }
      continue;
    }
    err = dEFSGetGlobalCoordinates(&efs[e],(const dReal(*)[3])(geom+geomoff[e]),&three,P,nx);dCHK(err);
    if (three != 3) dERROR(1,"Dimension not equal to 3");
    for (dInt i=0; i<P[0]-1; i++) { /* P-1 = number of sub-elements in each direction */
      for (dInt j=0; j<P[1]-1; j++) {
        for (dInt k=0; k<P[2]-1; k++) {
          dQ1CORNER_CONST_DECLARE(c,rowcol,corners,off[e],nx,P,i,j,k);
          const dReal (*qx)[3],*jw,(*basis)[8],(*deriv)[8][3];
          dInt qn;
          dScalar K[8][8],Ka[8][8];
          err = dMemzero(K,sizeof(K));dCHK(err);
          err = dMemzero(Ka,sizeof(Ka));dCHK(err);
          err = dQ1HexComputeQuadrature(corners,&qn,&qx,&jw,(const dReal**)&basis,(const dReal**)&deriv);dCHK(err);
          for (dInt lq=0; lq<qn; lq++) { /* loop over quadrature points */
            for (dInt ltest=0; ltest<8; ltest++) {              /* Loop over test basis functions (corners) */
              for (dInt lp=0; lp<8; lp++) {                     /* Loop over trial basis functions (corners) */
                dScalar pp = basis[lq][lp],
                  dp[3] = {deriv[lq][lp][0],
                           deriv[lq][lp][1],
                           deriv[lq][lp][2]};
                K[ltest][lp] += basis[lq][ltest] * jw[lq] * pp
                  + deriv[lq][ltest][0] * jw[lq] * 0 * dp[0]
                  + deriv[lq][ltest][1] * jw[lq] * 0 * dp[1]
                  + deriv[lq][ltest][2] * jw[lq] * 0 * dp[2];
                Ka[ltest][lp] += /* Auxiliary pressure-Poisson */
                  + deriv[lq][ltest][0] * jw[lq] * dp[0]
                  + deriv[lq][ltest][1] * jw[lq] * dp[1]
                  + deriv[lq][ltest][2] * jw[lq] * dp[2];
              }
            }
          }
          err = dFSMatSetValuesBlockedExpanded(fsp,D,8,rowcol,8,rowcol,&K[0][0],ADD_VALUES);dCHK(err);
          if (Daux) {err = dFSMatSetValuesBlockedExpanded(fsp,Daux,8,rowcol,8,rowcol,&Ka[0][0],ADD_VALUES);dCHK(err);}
        }
      }
    }
  }
  err = dFSRestoreWorkspace(fsp,__func__,&nx,NULL,NULL,NULL,NULL,NULL,NULL);dCHK(err);
  err = dFSRestoreElements(fsp,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = VecRestoreArray(stk->xu,&x);dCHK(err);

  err = MatAssemblyBegin(D,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyEnd  (D,MAT_FINAL_ASSEMBLY);dCHK(err);
  if (Daux) {
    err = MatAssemblyBegin(Daux,MAT_FINAL_ASSEMBLY);dCHK(err);
    err = MatAssemblyEnd  (Daux,MAT_FINAL_ASSEMBLY);dCHK(err);
  }
  dFunctionReturn(0);
}


static dErr StokesJacobian(SNES dUNUSED snes,Vec gx,Mat *J,Mat *Jp,MatStructure *structure,void *ctx)
{
  Stokes stk = ctx;
  Mat_StokesOuter *sms;
  dErr err;

  dFunctionBegin;
  err = MatShellGetContext(*Jp,(void**)&sms);dCHK(err);
  if (!stk->saddle_A_explicit) {
    Vec Mdiag;
    err = PetscObjectQuery((dObject)sms->D,"LSC_M_diag",(dObject*)&Mdiag);dCHK(err);
    err = StokesJacobianAssemble_Velocity(stk,*Jp,Mdiag,gx);dCHK(err);
  }
  {
    Mat S=sms->D,Saux;
    err = PetscObjectQuery((dObject)S,"LSC_L",(dObject*)&Saux);dCHK(err);
    err = StokesJacobianAssemble_Pressure(stk,S,Saux,gx);dCHK(err);
  }

  /* These are both shell matrices, we call this so SNES knows the matrices have changed */
  err = MatAssemblyBegin(*Jp,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyEnd(*Jp,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyBegin(*J,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyEnd(*J,MAT_FINAL_ASSEMBLY);dCHK(err);
  *structure = SAME_NONZERO_PATTERN;
  dFunctionReturn(0);
}

#if defined(ENABLE_PRECONDITIONING)
static dErr StokesErrorNorms(Stokes stk,Vec gx,dReal errorNorms[static 3],dReal gerrorNorms[static 3])
{
  dFS fs = stk->fs;
  dInt n,*off,*geomoff;
  s_dRule *rule;
  s_dEFS *efs;
  dReal (*geom)[3],(*q)[3],(*jinv)[3][3],*jw;
  dScalar *x,(*u)[3],(*du)[9];
  dErr err;

  dFunctionBegin;
  err = dMemzero(errorNorms,3*sizeof(errorNorms));dCHK(err);
  err = dMemzero(gerrorNorms,3*sizeof(gerrorNorms));dCHK(err);
  err = dFSGlobalToExpanded(fs,gx,stk->x,dFS_INHOMOGENEOUS,INSERT_VALUES);dCHK(err);
  err = VecGetArray(stk->x,&x);dCHK(err);
  err = dFSGetElements(fs,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = dFSGetWorkspace(fs,__func__,&q,&jinv,&jw,(dReal**)&u,NULL,(dReal**)&du,NULL);dCHK(err);
  for (dInt e=0; e<n; e++) {
    dInt Q;
    err = dRuleComputeGeometry(&rule[e],(const dReal(*)[3])(geom+geomoff[e]),q,jinv,jw);dCHK(err);
    err = dRuleGetSize(&rule[e],0,&Q);dCHK(err);
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,x+3*off[e],&u[0][0],dAPPLY_INTERP,INSERT_VALUES);dCHK(err);
    err = dEFSApply(&efs[e],(const dReal*)jinv,3,x+3*off[e],&du[0][0],dAPPLY_GRAD,INSERT_VALUES);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      dScalar uu[3],duu[9],r[3],gr[3],rsum=0,grsum=0;
      stk->exact.solution(&stk->exactctx,&stk->rheo,q[i],uu,duu);
      for (dInt j=0; j<3; j++) {
        r[j] = u[i][j] - uu[j]; /* Function error at point */
        rsum += dSqr(r[j]);
        gr[j] = dSqrt(dSqr(du[i][j*3+0]-duu[j*3+0]) + dSqr(du[i][j*3+1]-duu[j*3+1]) + dSqr(du[i][j*3+2]-duu[j*3+2])); /* Gradient error at point */
        grsum += dSqr(gr[j]);
      }
      if (stk->errorview) {
        printf("e,q = %3d %3d (% 5f,% 5f,% 5f) dohp %10.2e %10.2e %10.2e   exact %10.2e %10.2e %10.2e   error %10.e\n",
               e,i,q[i][0],q[i][1],q[i][2],u[i][0],u[i][1],u[i][2],uu[0],uu[1],uu[2],rsum);
      }
      errorNorms[0] += (dAbs(r[0]) + dAbs(r[1]) + dAbs(r[2])) * jw[i];                   /* 1-norm */
      errorNorms[1] += grsum * jw[i];                                                    /* 2-norm */
      errorNorms[2] = dMax(errorNorms[2],dMax(dAbs(r[0]),dMax(dAbs(r[1]),dAbs(r[2])))); /* Sup-norm */
      gerrorNorms[0] += (dAbs(gr[0]) + dAbs(gr[1]) + dAbs(gr[2])) * jw[i];
      gerrorNorms[1] += grsum * jw[i];
      gerrorNorms[2] = dMax(gerrorNorms[2],dMax(dAbs(gr[0]),dMax(dAbs(gr[1]),dAbs(gr[2]))));
#if 0
      printf("pointwise stats %8g %8g %8g %8g\n",jw[i],r[0],dSqr(r[0]),errorNorms[1]);
      printf("pointwise grads %8g %8g %8g (%8g)\n",gr[0],gr[1],gr[2],grsum);
# if 0
      printf("jinv[%2d][%3d]   %+3.1f %+3.1f %+3.1f    %+3.1f %+3.1f %+3.1f    %+3.1f %+3.1f %+3.1f\n",e,i,
             jinv[i][0][0],jinv[i][0][1],jinv[i][0][2],
             jinv[i][1][0],jinv[i][1][1],jinv[i][1][2],
             jinv[i][2][0],jinv[i][2][1],jinv[i][2][2]);
# endif
#endif
    }
  }
  err = dFSRestoreWorkspace(fs,__func__,&q,&jinv,&jw,(dReal**)&u,NULL,NULL,NULL);dCHK(err);
  err = dFSRestoreElements(fs,&n,&off,&rule,&efs,&geomoff,&geom);dCHK(err);
  err = VecRestoreArray(stk->x,&x);dCHK(err);
  errorNorms[1] = dSqrt(errorNorms[1]);
  gerrorNorms[1] = dSqrt(gerrorNorms[1]);
  dFunctionReturn(0);
}
#endif /* defined(ENABLE_PRECONDITIONING) */

static dErr PCSetUp_Stokes(PC pc)
{
  PC_Stokes       *pcs;
  Stokes           stk;
  Mat_StokesOuter *msfull,*msq1;
  dErr             err;

  dFunctionBegin;
  err = PCShellGetContext(pc,(void**)&pcs);dCHK(err);
  stk = pcs->stk;
  err = MatShellGetContext(pcs->J,(void**)&msfull);dCHK(err);
  err = MatShellGetContext(pcs->Jp,(void**)&msq1);dCHK(err);
  if (!pcs->kspA) {
    PC pcA;
    err = KSPCreate(stk->comm,&pcs->kspA);dCHK(err);
    err = KSPGetPC(pcs->kspA,&pcA);dCHK(err);
    err = PCSetType(pcA,PCNONE);dCHK(err);
    err = KSPSetOptionsPrefix(pcs->kspA,"saddle_A_");dCHK(err);
    err = KSPSetFromOptions(pcs->kspA);dCHK(err);
  }
  if (stk->saddle_A_explicit) {
    if (msq1->A) {err = MatDestroy(msq1->A);dCHK(err);}
    err = MatComputeExplicitOperator(msfull->A,&msq1->A);dCHK(err);
  }
  err = KSPSetOperators(pcs->kspA,msfull->A,msq1->A?msq1->A:msfull->A,SAME_NONZERO_PATTERN);dCHK(err);
  if (!pcs->S) {
    err = MatCreateSchurComplement(msfull->A,msq1->A,msfull->Bt,msfull->B,NULL,&pcs->S);dCHK(err);
    err = MatSetFromOptions(pcs->S);dCHK(err);
  }
  if (!pcs->kspS) {
    MatNullSpace matnull;
    PC           pcS;
    err = KSPCreate(stk->comm,&pcs->kspS);dCHK(err);
    err = KSPGetPC(pcs->kspS,&pcS);dCHK(err);
    err = PCSetType(pcS,PCNONE);dCHK(err);
    err = KSPSetOptionsPrefix(pcs->kspS,"saddle_S_");dCHK(err);
    err = KSPSetFromOptions(pcs->kspS);dCHK(err);
    /* constant pressure is in the null space of S */
    err = MatNullSpaceCreate(stk->comm,dTRUE,0,NULL,&matnull);dCHK(err);
    err = KSPSetNullSpace(pcs->kspS,matnull);dCHK(err);
    err = MatNullSpaceDestroy(matnull);dCHK(err);
  }
  err = KSPSetOperators(pcs->kspS,pcs->S,msq1->D?msq1->D:pcs->S,SAME_NONZERO_PATTERN);dCHK(err);
  dFunctionReturn(0);
}

static dErr PCApply_Stokes(PC pc,Vec x,Vec y)
{
  PC_Stokes       *pcs;
  Mat_StokesOuter *msfull;
  Stokes           stk;
  Vec              xu,xp,yu,yp;
  dErr             err;

  dFunctionBegin;
  err = PCShellGetContext(pc,(void**)&pcs);dCHK(err);
  stk = pcs->stk;
  xu = stk->gvelocity;
  xp = stk->gpressure;
  yu = stk->gvelocity_extra;
  yp = stk->gpressure_extra;
  err = MatShellGetContext(pcs->J,(void**)&msfull);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,x,xp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,x,xp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  err = KSPSolve(pcs->kspS,xp,yp);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,yp,y,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,yp,y,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = MatMultTranspose(msfull->B,xp,xu);dCHK(err);
  err = VecScale(xu,-1);dCHK(err);
  err = VecScatterBegin(stk->extractVelocity,x,xu,ADD_VALUES,SCATTER_FORWARD);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,x,xu,ADD_VALUES,SCATTER_FORWARD);dCHK(err);
  err = KSPSolve(pcs->kspA,xu,yu);dCHK(err);
  err = VecScatterBegin(stk->extractVelocity,yu,y,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,yu,y,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  dFunctionReturn(0);
}

static dErr PCDestroy_Stokes(PC pc)
{
  PC_Stokes *pcs;
  dErr err;

  dFunctionBegin;
  err = PCShellGetContext(pc,(void**)&pcs);dCHK(err);
  if (pcs->S)  {err = MatDestroy(pcs->S);dCHK(err);}
  if (pcs->kspA) {err = KSPDestroy(pcs->kspA);dCHK(err);}
  if (pcs->kspS) {err = KSPDestroy(pcs->kspS);dCHK(err);}
  err = dFree(pcs);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesGetSolutionField_All(Stokes stk,dFS fs,dTruth isvel,Vec *insoln)
{
  Vec      sol,xc,cvec;
  dScalar *x,*coords;
  dInt     n,bs;
  dErr     err;

  dFunctionBegin;
  *insoln = 0;
  err = dFSCreateGlobalVector(fs,&sol);dCHK(err);
  err = VecDohpGetClosure(sol,&xc);dCHK(err);
  err = dFSGetCoordinates(fs,&cvec);dCHK(err);
  err = VecGetLocalSize(xc,&n);dCHK(err);
  err = VecGetBlockSize(xc,&bs);dCHK(err);
  {
    dInt nc;
    err = VecGetLocalSize(cvec,&nc);dCHK(err);
    if (nc*bs != n*3) dERROR(1,"Coordinate vector has inconsistent size");
  }
  err = VecGetArray(xc,&x);dCHK(err);
  err = VecGetArray(cvec,&coords);dCHK(err);
  for (dInt i=0; i<n/bs; i++) {
    dScalar u_unused[3],p_unused[1],du_unused[3*3],dp_unused[3];
    /* if \a isvel then \a x is the velocity field, otherwise it is the pressure field */
    stk->exact.solution(&stk->exactctx,&stk->rheo,&coords[3*i],isvel ? &x[i*bs] : u_unused,isvel ? p_unused : &x[i*bs],du_unused,dp_unused);
    /* printf("Node %3d: coords %+8f %+8f %+8f   exact %+8f %+8f %+8f\n",i,coords[3*i],coords[3*i+1],coords[3*i+2],x[3*i],x[3*i+1],x[3*i+2]); */
  }
  err = VecRestoreArray(xc,&x);dCHK(err);
  err = VecRestoreArray(cvec,&coords);dCHK(err);
  err = VecDestroy(cvec);dCHK(err);
  err = dFSInhomogeneousDirichletCommit(fs,xc);dCHK(err);
  err = VecDohpRestoreClosure(sol,&xc);dCHK(err);
  *insoln = sol;
  dFunctionReturn(0);
}

/** Creates a solution vector, commits the closure to each FS, returns packed solution vector */
static dErr StokesGetSolutionVector(Stokes stk,Vec *insoln)
{
  dErr err;
  Vec solu,solp,spacked;

  dFunctionBegin;
  *insoln = 0;
  err = StokesGetSolutionField_All(stk,stk->fsu,dTRUE,&solu);dCHK(err);
  err = StokesGetSolutionField_All(stk,stk->fsp,dFALSE,&solp);dCHK(err);
  err = VecDuplicate(stk->gpacked,&spacked);dCHK(err);
  err = VecScatterBegin(stk->extractVelocity,solu,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,solu,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,solp,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,solp,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecDestroy(solu);dCHK(err);
  err = VecDestroy(solp);dCHK(err);
  *insoln = spacked;
  dFunctionReturn(0);
}

static dErr StokesGetNullSpace(Stokes stk,MatNullSpace *matnull)
{
  dErr err;
  Vec r;

  dFunctionBegin;
  err = VecDuplicate(stk->gpacked,&r);dCHK(err);
  err = VecZeroEntries(r);dCHK(err);
  err = VecSet(stk->gpressure,1);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,stk->gpressure,r,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,stk->gpressure,r,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecNormalize(r,PETSC_NULL);dCHK(err);
  err = MatNullSpaceCreate(stk->comm,dFALSE,1,&r,matnull);dCHK(err);
  err = VecDestroy(r);dCHK(err);
  dFunctionReturn(0);
}


static dErr CheckNullSpace(SNES snes,Vec residual,dTruth compute_explicit)
{
  Mat          mffd,J,Jp;
  dTruth       isnull;
  Vec          U,F;
  MatStructure mstruct;
  MatNullSpace matnull;
  KSP          ksp;
  dErr         err;

  dFunctionBegin;
  err = SNESGetKSP(snes,&ksp);dCHK(err);
  err = KSPGetNullSpace(ksp,&matnull);dCHK(err);
  err = MatCreateSNESMF(snes,&mffd);dCHK(err);
  err = MatSetFromOptions(mffd);dCHK(err);
  {
    err = VecDuplicate(residual,&U);dCHK(err);
    err = VecDuplicate(residual,&F);dCHK(err);
  }
  err = SNESGetJacobian(snes,&J,&Jp,NULL,NULL);dCHK(err);
  err = VecSet(U,0);dCHK(err);
  err = SNESComputeFunction(snes,U,F);dCHK(err); /* Need base for MFFD */
  err = MatMFFDSetBase(mffd,U,F);dCHK(err);
  err = MatNullSpaceTest(matnull,mffd,&isnull);dCHK(err);
  if (!isnull) dERROR(1,"Vector is not in the null space of the MFFD operator");dCHK(err);
  err = MatNullSpaceTest(matnull,J,&isnull);dCHK(err);
  if (!isnull) dERROR(1,"Vector is not in the null space of J");dCHK(err);
  err = SNESComputeJacobian(snes,U,&J,&Jp,&mstruct);dCHK(err); /* To assemble blocks of Jp */
  err = MatNullSpaceTest(matnull,Jp,&isnull);dCHK(err);
  if (!isnull) dERROR(1,"Vector is not in the null space of Jp");dCHK(err);
  err = MatNullSpaceDestroy(matnull);dCHK(err);
  err = MatDestroy(mffd);dCHK(err);
  if (compute_explicit) {
    Mat expmat,expmat_fd;
    dInt m,n;
    dTruth contour = dFALSE;
    err = MatGetLocalSize(J,&m,&n);dCHK(err);
    err = MatComputeExplicitOperator(J,&expmat);dCHK(err);
    err = MatDuplicate(expmat,MAT_DO_NOT_COPY_VALUES,&expmat_fd);dCHK(err);
    err = SNESDefaultComputeJacobian(snes,U,&expmat_fd,&expmat_fd,&mstruct,NULL);dCHK(err);
    err = MatSetOptionsPrefix(expmat,"explicit_");dCHK(err);
    err = MatSetOptionsPrefix(expmat_fd,"explicit_fd_");dCHK(err);
    err = MatSetFromOptions(expmat);dCHK(err);
    err = MatSetFromOptions(expmat_fd);dCHK(err);

    err = PetscOptionsGetTruth(NULL,"-mat_view_contour",&contour,NULL);dCHK(err);
    if (contour) {err = PetscViewerPushFormat(PETSC_VIEWER_DRAW_WORLD,PETSC_VIEWER_DRAW_CONTOUR);dCHK(err);}
    {
      dTruth flg = dFALSE;
      err = PetscOptionsGetTruth(NULL,"-explicit_mat_view",&flg,NULL);dCHK(err);
      if (flg) {
        err = PetscViewerASCIIPrintf(PETSC_VIEWER_STDOUT_WORLD,"###  Explicit matrix using mat-free implementation of J\n");dCHK(err);
        err = MatView(expmat,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
      }
      flg = dFALSE;
      err = PetscOptionsGetTruth(NULL,"-explicit_mat_view_draw",&flg,NULL);dCHK(err);
      if (flg) {err = MatView(expmat,PETSC_VIEWER_DRAW_WORLD);dCHK(err);}
    }

    {
      dTruth flg = dFALSE;
      err = PetscOptionsGetTruth(NULL,"-explicit_fd_mat_view",&flg,NULL);dCHK(err);
      if (flg) {
        err = PetscViewerASCIIPrintf(PETSC_VIEWER_STDOUT_WORLD,"###  Explicit matrix using FD\n");dCHK(err);
        err = MatView(expmat_fd,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
      }
      flg = dFALSE;
      err = PetscOptionsGetTruth(NULL,"-explicit_fd_mat_view_draw",&flg,NULL);dCHK(err);
      if (flg) {err = MatView(expmat_fd,PETSC_VIEWER_DRAW_WORLD);dCHK(err);}
    }

    err = MatAXPY(expmat,-1,expmat_fd,SAME_NONZERO_PATTERN);dCHK(err);
    {
      dTruth flg = dFALSE;
      err = PetscOptionsGetTruth(NULL,"-explicit_diff_mat_view",&flg,NULL);dCHK(err);
      if (flg) {
        err = PetscViewerASCIIPrintf(PETSC_VIEWER_STDOUT_WORLD,"###  Difference between mat-free implementation of J and FD\n");dCHK(err);
        err = MatView(expmat,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
      }
      flg = dFALSE;
      err = PetscOptionsGetTruth(NULL,"-explicit_diff_mat_view_draw",&flg,NULL);dCHK(err);
      if (flg) {err = MatView(expmat,PETSC_VIEWER_DRAW_WORLD);dCHK(err);}
    }
    if (contour) {err = PetscViewerPopFormat(PETSC_VIEWER_DRAW_WORLD);dCHK(err);}
    err = MatDestroy(expmat);dCHK(err);
    err = MatDestroy(expmat_fd);dCHK(err);
  }
  err = VecDestroy(U);dCHK(err);
  err = VecDestroy(F);dCHK(err);
  dFunctionReturn(0);
}


int main(int argc,char *argv[])
{
  Stokes stk;
  MPI_Comm comm;
  PetscViewer viewer;
  Mat J,Jp;
  MatFDColoring fdcolor = 0;
  Vec r,x,soln;
  SNES snes;
  dTruth nocheck,check_null,compute_explicit,use_jblock;
  dErr err;

  err = PetscInitialize(&argc,&argv,NULL,help);dCHK(err);
  comm = PETSC_COMM_WORLD;
  viewer = PETSC_VIEWER_STDOUT_WORLD;
  err = PetscLogEventRegister("StokesShellMult",MAT_COOKIE,&LOG_StokesShellMult);dCHK(err);

  err = StokesCreate(comm,&stk);dCHK(err);
  err = StokesSetFromOptions(stk);dCHK(err);

  err = VecDuplicate(stk->gpacked,&r);dCHK(err);
  err = VecDuplicate(r,&x);dCHK(err);

  err = PetscOptionsBegin(stk->comm,NULL,"Stokes solver options",__FILE__);dCHK(err); {
    err = PetscOptionsName("-nocheck_error","Do not compute errors","",&nocheck);dCHK(err);
    err = PetscOptionsName("-use_jblock","Use blocks to apply Jacobian instead of unified (more efficient) version","",&use_jblock);dCHK(err);
    err = PetscOptionsName("-check_null","Check that constant pressure really is in the null space","",&check_null);dCHK(err);
    if (check_null) {
      err = PetscOptionsName("-compute_explicit","Compute explicit Jacobian (only very small sizes)","",&compute_explicit);dCHK(err);
    }
  } err = PetscOptionsEnd();dCHK(err);
  err = StokesGetMatrices(stk,use_jblock,&J,&Jp);dCHK(err);
  err = SNESCreate(comm,&snes);dCHK(err);
  err = SNESSetFunction(snes,r,StokesFunction,stk);dCHK(err);
  switch (3) {
    case 1:
      err = SNESSetJacobian(snes,J,Jp,SNESDefaultComputeJacobian,stk);dCHK(err); break;
    case 2: {
      ISColoring iscolor;
      err = MatGetColoring(Jp,MATCOLORING_ID,&iscolor);dCHK(err);
      err = MatFDColoringCreate(Jp,iscolor,&fdcolor);dCHK(err);
      err = ISColoringDestroy(iscolor);dCHK(err);
      err = MatFDColoringSetFunction(fdcolor,(PetscErrorCode(*)(void))StokesFunction,stk);dCHK(err);
      err = MatFDColoringSetFromOptions(fdcolor);dCHK(err);
      err = SNESSetJacobian(snes,J,Jp,SNESDefaultComputeJacobianColor,fdcolor);dCHK(err);
    } break;
    case 3:
      err = SNESSetJacobian(snes,J,Jp,StokesJacobian,stk);dCHK(err);
      break;
    default: dERROR(1,"Not supported");
  }
  err = SNESSetFromOptions(snes);dCHK(err);
  {
    KSP    ksp;
    PC     pc;
    dTruth isshell;

    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPGetPC(ksp,&pc);dCHK(err);
    err = PCFieldSplitSetIS(pc,stk->ublock);dCHK(err);
    err = PCFieldSplitSetIS(pc,stk->pblock);dCHK(err);
    err = PetscTypeCompare((dObject)pc,PCSHELL,&isshell);dCHK(err);
    if (isshell) {
      PC_Stokes *pcs;
      err = dNew(PC_Stokes,&pcs);dCHK(err);
      pcs->J   = J;
      pcs->Jp  = Jp;
      pcs->stk = stk;
      err = PCShellSetContext(pc,pcs);dCHK(err);
      err = PCShellSetApply(pc,PCApply_Stokes);dCHK(err);
      err = PCShellSetSetUp(pc,PCSetUp_Stokes);dCHK(err);
      err = PCShellSetDestroy(pc,PCDestroy_Stokes);dCHK(err);
    }
  }
  err = StokesGetSolutionVector(stk,&soln);dCHK(err);
  {
    dReal nrm;
    MatStructure mstruct;
    Vec b;
    err = VecDuplicate(x,&b);dCHK(err);
    err = VecZeroEntries(x);dCHK(err);
    err = SNESComputeFunction(snes,x,b);dCHK(err); /* -f */
    err = SNESComputeFunction(snes,soln,r);dCHK(err);
    err = VecNorm(r,NORM_2,&nrm);dCHK(err);
    err = dPrintf(comm,"Norm of discrete residual for exact solution %g\n",nrm);dCHK(err);
    err = SNESComputeJacobian(snes,soln,&J,&Jp,&mstruct);dCHK(err);
    err = MatMult(J,soln,r);dCHK(err);
    err = VecAXPY(r,1,b);dCHK(err); /* Jx - f */
    err = VecNorm(r,NORM_2,&nrm);dCHK(err);
    err = dPrintf(comm,"Norm of discrete linear residual at exact solution %g\n",nrm);dCHK(err);
    err = VecDestroy(b);dCHK(err);
  }

  if (!stk->neumann300) {                             /* Set null space */
    KSP ksp;
    MatNullSpace matnull;
    err = StokesGetNullSpace(stk,&matnull);dCHK(err);
    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPSetNullSpace(ksp,matnull);dCHK(err);
    err = MatNullSpaceRemove(matnull,soln,NULL);dCHK(err);
    err = MatNullSpaceDestroy(matnull);dCHK(err);
  }
  if (check_null) {
    err = CheckNullSpace(snes,r,compute_explicit);dCHK(err);
  }
  err = VecZeroEntries(r);dCHK(err);
  err = VecZeroEntries(x);dCHK(err);
  err = SNESSolve(snes,NULL,x);dCHK(err); /* ###  SOLVE  ### */
  if (0) {
    MatNullSpace matnull;
    KSP ksp;
    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPGetNullSpace(ksp,&matnull);dCHK(err); /* does not reference */
    err = MatNullSpaceRemove(matnull,x,NULL);dCHK(err);
  }
  if (!nocheck) {
    dReal anorm[2],anorminf,inorm[3];//,enorm[3],gnorm[3];
    //err = StokesErrorNorms(stk,x,enorm,gnorm);dCHK(err);
    err = VecNorm(r,NORM_1_AND_2,anorm);dCHK(err);
    err = VecNorm(r,NORM_INFINITY,&anorminf);dCHK(err);
    err = VecWAXPY(r,-1,soln,x);dCHK(err);
    err = VecNorm(r,NORM_1_AND_2,inorm);dCHK(err);
    err = VecNorm(r,NORM_INFINITY,&inorm[2]);dCHK(err);
    err = dPrintf(comm,"Algebraic residual        |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",anorm[0],anorm[1],anorminf);dCHK(err);
    err = dPrintf(comm,"Interpolation residual    |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",inorm[0],inorm[1],inorm[2]);dCHK(err);
    //err = dPrintf(comm,"Pointwise solution error  |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",enorm[0],enorm[1],enorm[2]);dCHK(err);
    //err = dPrintf(comm,"Pointwise gradient error  |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",gnorm[0],gnorm[1],gnorm[2]);dCHK(err);
  }

  err = VecDestroy(r);dCHK(err);
  err = VecDestroy(x);dCHK(err);
  err = VecDestroy(soln);dCHK(err);
  err = SNESDestroy(snes);dCHK(err);
  if (fdcolor) {err = MatFDColoringDestroy(fdcolor);dCHK(err);}
  if (J != Jp) {err = MatDestroy(J);dCHK(err);}
  err = MatDestroy(Jp);dCHK(err);
  err = StokesDestroy(stk);dCHK(err);
  err = PetscFinalize();dCHK(err);
  return 0;
}