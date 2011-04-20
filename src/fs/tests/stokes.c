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

#include <petscsnes.h>
#include <dohpfs.h>
#include <dohpvec.h>
#include <dohpsys.h>
#include <dohpstring.h>
#include <dohp.h>

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
                                   dScalar u[],dScalar du[],dScalar p[],dScalar dp[])
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
                                   dScalar u[],dScalar du[],dScalar p[],dScalar dp[])
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

typedef enum {EVAL_FUNCTION,EVAL_JACOBIAN, EVAL_UB} StokesEvaluation;
typedef enum {STOKES_MULT_A,STOKES_MULT_Bt,STOKES_MULT_B} StokesMultMode;

static dErr StokesGetNullSpace(Stokes stk,MatNullSpace *matnull);
static dErr StokesShellMatMult_All_IorA(Mat A,Vec gx,Vec gy,Vec gz,InsertMode,StokesMultMode);
static dErr MatMult_Nest_StokesCoupled(Mat J,Vec gx,Vec gy);
static dErr StokesShellMatMult_A(Mat A,Vec gx,Vec gy) {return StokesShellMatMult_All_IorA(A,gx,gy,NULL,INSERT_VALUES,STOKES_MULT_A);}
static dErr StokesShellMatMult_Bt(Mat A,Vec gx,Vec gy) {return StokesShellMatMult_All_IorA(A,gx,gy,NULL,INSERT_VALUES,STOKES_MULT_Bt);}
static dErr StokesShellMatMult_B(Mat A,Vec gx,Vec gy) {return StokesShellMatMult_All_IorA(A,gx,gy,NULL,INSERT_VALUES,STOKES_MULT_B);}
static dErr StokesShellMatMultAdd_A(Mat A,Vec gx,Vec gy,Vec gz) {return StokesShellMatMult_All_IorA(A,gx,gy,gz,ADD_VALUES,STOKES_MULT_A);}
static dErr StokesShellMatMultAdd_Bt(Mat A,Vec gx,Vec gy,Vec gz) {return StokesShellMatMult_All_IorA(A,gx,gy,gz,ADD_VALUES,STOKES_MULT_Bt);}
static dErr StokesShellMatMultAdd_B(Mat A,Vec gx,Vec gy,Vec gz) {return StokesShellMatMult_All_IorA(A,gx,gy,gz,ADD_VALUES,STOKES_MULT_B);}
static dErr MatGetVecs_Stokes(Mat,Vec*,Vec*);

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
  dInt                   constBDeg,pressureCodim;
  dBool                  errorview,cardinalMass,neumann300;
  char                   mattype_A[256],mattype_D[256];
  dQuadratureMethod      function_qmethod,jacobian_qmethod;
  dRulesetIterator       regioniter[EVAL_UB];
};

static dErr StokesCreate(MPI_Comm comm,Stokes *stokes)
{
  Stokes stk;
  dErr err;

  dFunctionBegin;
  *stokes = 0;
  err = dNew(struct _p_Stokes,&stk);dCHK(err);
  stk->comm = comm;

  stk->constBDeg     = 3;
  stk->pressureCodim = 2;
  stk->rheo.A        = 1;
  stk->rheo.eps      = 1;
  stk->rheo.p        = 2;
  stk->function_qmethod = dQUADRATURE_METHOD_FAST;
  stk->jacobian_qmethod = dQUADRATURE_METHOD_SPARSE;

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
  if (nu==np) dERROR(PETSC_COMM_SELF,1,"Degenerate case, don't know which space to copy");
  if (x) {
    if (n == nu) {
      err = VecDuplicate(stk->gvelocity,x);dCHK(err);
    } else if (n == np) {
      err = VecDuplicate(stk->gpressure,x);dCHK(err);
    } else dERROR(PETSC_COMM_SELF,1,"sizes do not agree with either space");
  }
  if (y) {
    if (n == nu) {
      err = VecDuplicate(stk->gvelocity,y);dCHK(err);
    } else if (n == np) {
      err = VecDuplicate(stk->gpressure,y);dCHK(err);
    } else dERROR(PETSC_COMM_SELF,1,"sizes do not agree with either space");
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
  dMeshTag dtag,dptag;
  dInt exact;
  dErr err;

  dFunctionBegin;
  exact = 0; exc->a = exc->b = exc->c = 1;
  err = dStrcpyS(stk->mattype_A,sizeof(stk->mattype_A),MATBAIJ);dCHK(err);
  err = dStrcpyS(stk->mattype_D,sizeof(stk->mattype_D),MATAIJ);dCHK(err);
  err = PetscOptionsBegin(stk->comm,NULL,"Stokesicity options",__FILE__);dCHK(err); {
    err = PetscOptionsInt("-const_bdeg","Use constant isotropic degree on all elements","",stk->constBDeg,&stk->constBDeg,NULL);dCHK(err);
    err = PetscOptionsInt("-pressure_codim","Reduce pressure space by this factor","",stk->pressureCodim,&stk->pressureCodim,NULL);dCHK(err);
    err = PetscOptionsBool("-cardinal_mass","Assemble diagonal mass matrix","",stk->cardinalMass,&stk->cardinalMass,NULL);dCHK(err);
    err = PetscOptionsBool("-error_view","View errors","",stk->errorview,&stk->errorview,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_A","Rate factor (rheology)","",rheo->A,&rheo->A,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_eps","Regularization (rheology)","",rheo->eps,&rheo->eps,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_p","Power p=1+1/n where n is Glen exponent","",rheo->p,&rheo->p,NULL);dCHK(err);
    err = PetscOptionsInt("-exact","Exact solution choice","",exact,&exact,NULL);dCHK(err);
    err = PetscOptionsReal("-exact_a","First scale parameter","",exc->a,&exc->a,NULL);dCHK(err);
    err = PetscOptionsReal("-exact_b","Second scale parameter","",exc->b,&exc->b,NULL);dCHK(err);
    err = PetscOptionsReal("-exact_c","Third scale parameter","",exc->c,&exc->c,NULL);dCHK(err);
    err = PetscOptionsList("-stokes_A_mat_type","Matrix type for velocity operator","",MatList,stk->mattype_A,stk->mattype_A,sizeof(stk->mattype_A),NULL);dCHK(err);
    err = PetscOptionsList("-stokes_D_mat_type","Matrix type for velocity operator","",MatList,stk->mattype_D,stk->mattype_D,sizeof(stk->mattype_D),NULL);dCHK(err);
    err = PetscOptionsEnum("-stokes_f_qmethod","Quadrature method for residual evaluation/matrix-free","",dQuadratureMethods,(PetscEnum)stk->function_qmethod,(PetscEnum*)&stk->function_qmethod,NULL);dCHK(err);
    err = PetscOptionsEnum("-stokes_jac_qmethod","Quadrature to use for Jacobian assembly","",dQuadratureMethods,(PetscEnum)stk->jacobian_qmethod,(PetscEnum*)&stk->jacobian_qmethod,NULL);dCHK(err);
    err = PetscOptionsBool("-neumann300","Use boundary set 300 as Neumann conditions","",stk->neumann300,&stk->neumann300,NULL);dCHK(err);
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
    default: dERROR(PETSC_COMM_SELF,1,"Exact solution %d not implemented");
  }

  err = dMeshCreate(stk->comm,&mesh);dCHK(err);
  err = dMeshSetInFile(mesh,"dblock.h5m",NULL);dCHK(err);
  err = dMeshSetFromOptions(mesh);dCHK(err);
  err = dMeshLoad(mesh);dCHK(err);dCHK(err);
  stk->mesh = mesh;
  err = dMeshGetRoot(mesh,&domain);dCHK(err); /* Need a taggable set */
  err = dMeshSetDuplicateEntsOnly(mesh,domain,&domain);dCHK(err);

  err = dJacobiCreate(stk->comm,&jac);dCHK(err);
  err = dJacobiSetFromOptions(jac);dCHK(err);
  stk->jac = jac;

  err = dMeshCreateRuleTagIsotropic(mesh,domain,"stokes_efs_velocity_degree",stk->constBDeg,&dtag);dCHK(err);
  err = dMeshCreateRuleTagIsotropic(mesh,domain,"stokes_efs_pressure_degree",stk->constBDeg-stk->pressureCodim,&dptag);dCHK(err);

  err = dFSCreate(stk->comm,&fsu);dCHK(err);
  err = dFSSetBlockSize(fsu,3);dCHK(err);
  err = dFSSetMesh(fsu,mesh,domain);dCHK(err);
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
  err = dFSSetDegree(fsp,jac,dptag);dCHK(err);
  err = PetscObjectSetOptionsPrefix((dObject)fsp,"p");dCHK(err);
  /* No boundaries, the pressure space has Neumann conditions when Dirichlet velocity conditions are applied */
  err = dFSSetFromOptions(fsp);dCHK(err);
  stk->fsp = fsp;

  err = dFSCreateExpandedVector(fsu,&stk->xu);dCHK(err);
  err = VecDuplicate(stk->xu,&stk->yu);dCHK(err);

  err = dFSCreateExpandedVector(fsp,&stk->xp);dCHK(err);
  err = VecDuplicate(stk->xp,&stk->yp);dCHK(err);

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

static dErr StokesGetRegionIterator(Stokes stk,StokesEvaluation eval,dRulesetIterator *riter)
{
  dErr err;

  dFunctionBegin;
  if (!stk->regioniter[eval]) {
    dRulesetIterator iter;
    dRuleset ruleset;
    dFS cfs;
    dMeshESH domain;
    dQuadratureMethod qmethod;
    switch (eval) {
    case EVAL_FUNCTION: qmethod = stk->function_qmethod; break;
    case EVAL_JACOBIAN: qmethod = stk->jacobian_qmethod; break;
    default: dERROR(stk->comm,PETSC_ERR_ARG_OUTOFRANGE,"Unknown evaluation context");
    }
    err = dFSGetDomain(stk->fsu,&domain);dCHK(err);
    err = dFSGetPreferredQuadratureRuleSet(stk->fsu,domain,dTYPE_REGION,dTOPO_ALL,qmethod,&ruleset);dCHK(err);
    err = dFSGetCoordinateFS(stk->fsu,&cfs);dCHK(err);
    err = dRulesetCreateIterator(ruleset,cfs,&iter);dCHK(err);
    err = dRulesetDestroy(ruleset);dCHK(err); /* Give ownership to iterator */
    err = dRulesetIteratorAddFS(iter,stk->fsu);dCHK(err);
    err = dRulesetIteratorAddFS(iter,stk->fsp);dCHK(err);
    if (eval == EVAL_FUNCTION) {err = dRulesetIteratorAddStash(iter,0,sizeof(struct StokesStore));dCHK(err);}
    stk->regioniter[eval] = iter;
  }
  *riter = stk->regioniter[eval];
  dFunctionReturn(0);
}

static dErr StokesExtractGlobalSplit(Stokes stk,Vec gx,Vec *gxu,Vec *gxp)
{
  dErr err;

  dFunctionBegin;
  if (gxu) {
    *gxu = stk->gvelocity;
    err = VecScatterBegin(stk->extractVelocity,gx,*gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
    err = VecScatterEnd  (stk->extractVelocity,gx,*gxu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  }
  if (gxp) {
    *gxp = stk->gpressure;
    err = VecScatterBegin(stk->extractPressure,gx,*gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
    err = VecScatterEnd  (stk->extractPressure,gx,*gxp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  }
  dFunctionReturn(0);
}

static dErr StokesCommitGlobalSplit(Stokes stk,Vec *gxu,Vec *gxp,Vec gy,InsertMode imode)
{
  dErr err;

  dFunctionBegin;
  dASSERT(*gxu == stk->gvelocity);
  dASSERT(*gxp == stk->gpressure);
  err = VecScatterBegin(stk->extractVelocity,*gxu,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractVelocity,*gxu,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterBegin(stk->extractPressure,*gxp,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (stk->extractPressure,*gxp,gy,imode,SCATTER_REVERSE);dCHK(err);
  *gxu = NULL;
  *gxp = NULL;
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

static dErr StokesGetMatrices(Stokes stk,dBool use_jblock,Mat *J,Mat *Jp)
{
  dErr err;
  dInt m,nu,np;
  Mat A,B,Bt,D;
  IS splitis[2];

  dFunctionBegin;
  err = VecGetLocalSize(stk->gpacked,&m);dCHK(err);
  err = VecGetLocalSize(stk->gvelocity,&nu);dCHK(err);
  err = VecGetLocalSize(stk->gpressure,&np);dCHK(err);

  /* Create high-order matrix for diagonal velocity block, with context \a stk */
  err = MatCreateShell(stk->comm,nu,nu,PETSC_DETERMINE,PETSC_DETERMINE,stk,&A);dCHK(err);
  err = MatShellSetOperation(A,MATOP_GET_VECS,(void(*)(void))MatGetVecs_Stokes);dCHK(err);
  err = MatShellSetOperation(A,MATOP_MULT,(void(*)(void))StokesShellMatMult_A);dCHK(err);
  err = MatShellSetOperation(A,MATOP_MULT_TRANSPOSE,(void(*)(void))StokesShellMatMult_A);dCHK(err);
  err = MatShellSetOperation(A,MATOP_MULT_ADD,(void(*)(void))StokesShellMatMultAdd_A);dCHK(err);
  err = MatShellSetOperation(A,MATOP_MULT_TRANSPOSE_ADD,(void(*)(void))StokesShellMatMultAdd_A);dCHK(err);
  err = MatSetOptionsPrefix(A,"A_");dCHK(err);

  /* Create off-diagonal high-order matrix, with context \a stk */
  err = MatCreateShell(stk->comm,np,nu,PETSC_DETERMINE,PETSC_DETERMINE,stk,&B);dCHK(err);
  err = MatShellSetOperation(B,MATOP_GET_VECS,(void(*)(void))MatGetVecs_Stokes);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT,(void(*)(void))StokesShellMatMult_B);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT_TRANSPOSE,(void(*)(void))StokesShellMatMult_Bt);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT_ADD,(void(*)(void))StokesShellMatMultAdd_B);dCHK(err);
  err = MatShellSetOperation(B,MATOP_MULT_TRANSPOSE_ADD,(void(*)(void))StokesShellMatMultAdd_Bt);dCHK(err);
  err = MatCreateTranspose(B,&Bt);dCHK(err);
  err = MatSetOptionsPrefix(B,"B_");dCHK(err);
  err = MatSetOptionsPrefix(Bt,"Bt_");dCHK(err);

  splitis[0] = stk->ublock;
  splitis[1] = stk->pblock;
  /* Create the matrix-free operator */
  err = MatCreateNest(stk->comm,2,splitis,2,splitis,((Mat[]){A,Bt,B,NULL}),J);dCHK(err);
  err = MatSetOptionsPrefix(*J,"J_");dCHK(err);
  err = MatSetFromOptions(*J);dCHK(err);
  if (!use_jblock) {
    err = MatShellSetOperation(*J,MATOP_MULT,(void(*)(void))MatMult_Nest_StokesCoupled);dCHK(err);
    err = MatShellSetOperation(*J,MATOP_MULT_TRANSPOSE,(void(*)(void))MatMult_Nest_StokesCoupled);dCHK(err);
  }

  err = MatDestroy(A);dCHK(err);
  err = MatDestroy(Bt);dCHK(err);
  err = MatDestroy(B);dCHK(err);

  /* Create real matrix to be used for preconditioning */
  err = dFSGetMatrix(stk->fsu,stk->mattype_A,&A);dCHK(err);
  err = dFSGetMatrix(stk->fsp,stk->mattype_D,&D);dCHK(err);
  err = MatSetOptionsPrefix(A,"Ap_");dCHK(err);
  err = MatSetOptionsPrefix(D,"Dp_");dCHK(err);
  err = MatCreateNest(stk->comm,2,splitis,2,splitis,((Mat[]){A,NULL,NULL,D}),Jp);dCHK(err);
  err = MatSetOptionsPrefix(*Jp,"Jp_");dCHK(err);
  err = MatSetFromOptions(*Jp);dCHK(err);

  {                             /* Allocate for the pressure Poisson, used by PCLSC */
    Mat L;
    Vec Mdiag;
    err = dFSGetMatrix(stk->fsp,stk->mattype_D,&L);dCHK(err);
    err = MatSetOptionsPrefix(L,"stokes_L_");dCHK(err);
    err = MatSetFromOptions(L);dCHK(err);
    err = PetscObjectCompose((dObject)D,"LSC_L",(dObject)L);dCHK(err);
    err = PetscObjectCompose((dObject)D,"LSC_Lp",(dObject)L);dCHK(err);
    err = MatDestroy(L);dCHK(err); /* don't keep a reference */
    err = VecDuplicate(stk->gvelocity,&Mdiag);dCHK(err);
    err = PetscObjectCompose((dObject)D,"LSC_M_diag",(dObject)Mdiag);
    err = VecDestroy(Mdiag);dCHK(err); /* don't keep a reference */
  }

  err = MatDestroy(A);dCHK(err); /* release reference to Jp */
  err = MatDestroy(D);dCHK(err); /* release reference to Jp */
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
  Stokes           stk = ctx;
  dErr             err;
  Vec              Coords,gxu,gxp;
  dRulesetIterator iter;

  dFunctionBegin;
  err = StokesExtractGlobalSplit(stk,gx,&gxu,&gxp);dCHK(err);
  err = StokesGetRegionIterator(stk,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(stk->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gxu,dFS_INHOMOGENEOUS,gxu,dFS_INHOMOGENEOUS, gxp,dFS_INHOMOGENEOUS,gxp,dFS_INHOMOGENEOUS);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*u)[3],(*du)[9],(*v)[3],(*dv)[9],*p,*q;
    dInt Q;
    struct StokesStore *stash;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,&v,&dv, &p,NULL,&q,NULL);dCHK(err);dCHK(err);
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      dScalar Du[6],Dv[6];
      dTensorSymCompress3(du[i],Du);
      StokesPointwiseFunction(&stk->rheo,&stk->exact,&stk->exactctx,x[i],jw[i],Du,p[i],&stash[i],v[i],Dv,&q[i]);
      dTensorSymUncompress3(Dv,dv[i]);
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL,v,dv, NULL,NULL,q,NULL);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = StokesCommitGlobalSplit(stk,&gxu,&gxp,gy,INSERT_VALUES);dCHK(err);
  dFunctionReturn(0);
}

static dErr MatMult_Nest_StokesCoupled(Mat J,Vec gx,Vec gy)
{
  Stokes           stk;
  Vec              Coords,gxu,gxp;
  dRulesetIterator iter;
  dErr             err;
  Mat              A;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_StokesShellMult,J,gx,gy,0);dCHK(err);
  err = MatNestGetSubMat(J,0,0,&A);dCHK(err);
  err = MatShellGetContext(A,(void**)&stk);dCHK(err);
  err = StokesExtractGlobalSplit(stk,gx,&gxu,&gxp);dCHK(err);
  err = StokesGetRegionIterator(stk,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(stk->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gxu,dFS_HOMOGENEOUS,gxu,dFS_HOMOGENEOUS, gxp,dFS_HOMOGENEOUS,gxp,dFS_HOMOGENEOUS);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*u)[3],(*du)[9],(*dv)[9],*p,*q;
    dInt Q;
    struct StokesStore *stash;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,NULL,&dv, &p,NULL,&q,NULL);dCHK(err);dCHK(err);
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      dScalar Du[6],Dv[6];
      dTensorSymCompress3(du[i],Du);
      StokesPointwiseJacobian(&stash[i],jw[i],Du,p[i],Dv,&q[i]);
      dTensorSymUncompress3(Dv,dv[i]);
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL,NULL,dv, NULL,NULL,q,NULL);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = StokesCommitGlobalSplit(stk,&gxu,&gxp,gy,INSERT_VALUES);dCHK(err);
  err = PetscLogEventEnd(LOG_StokesShellMult,J,gx,gy,0);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesShellMatMult_All_IorA(Mat A,Vec gx,Vec gy,Vec gz,InsertMode imode,StokesMultMode mmode)
{
  Stokes           stk;
  dRulesetIterator iter;
  Vec              Coords;
  dErr             err;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_StokesShellMult,A,gx,gy,gz);dCHK(err);
  err = MatShellGetContext(A,(void**)&stk);dCHK(err);
  {  /* Check that we have correct sizes */
    dInt nu,np,nx,ny;
    err = VecGetSize(stk->gvelocity,&nu);dCHK(err);
    err = VecGetSize(stk->gpressure,&np);dCHK(err);
    err = VecGetSize(gx,&nx);dCHK(err);
    err = VecGetSize(gy,&ny);dCHK(err);
    switch (mmode) {
    case STOKES_MULT_A: dASSERT(nx==nu && ny==nu); break;
    case STOKES_MULT_Bt: dASSERT(nx==np && ny==nu); break;
    case STOKES_MULT_B: dASSERT(nx==nu && ny==np); break;
    default: dERROR(PETSC_COMM_SELF,1,"Sizes do not match, unknown mult operation");
    }
  }

  switch (imode) {
  case INSERT_VALUES:
    if (gz) dERROR(stk->comm,PETSC_ERR_ARG_INCOMP,"Cannot use INSERT_VALUES and set gz");
    gz = gy;
    err = VecZeroEntries(gz);dCHK(err);
    break;
  case ADD_VALUES:
    if (gz != gy) {
      err = VecCopy(gy,gz);dCHK(err);
    }
    break;
  default: dERROR(stk->comm,PETSC_ERR_ARG_OUTOFRANGE,"unsupported imode");
  }

  err = StokesGetRegionIterator(stk,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(stk->fsu,&Coords);dCHK(err);
  switch (mmode) {
  case STOKES_MULT_A:
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gx,dFS_HOMOGENEOUS,gz,dFS_HOMOGENEOUS, NULL,NULL);dCHK(err);
    break;
  case STOKES_MULT_Bt:
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, NULL,gz,dFS_HOMOGENEOUS, gx,dFS_HOMOGENEOUS,NULL);dCHK(err);
    break;
  case STOKES_MULT_B:
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gx,dFS_HOMOGENEOUS,NULL, NULL,gz,dFS_HOMOGENEOUS);dCHK(err);
    break;
  default: dERROR(stk->comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid mmode");
  }
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*du)[9],(*dv)[9],*p,*q;
    dInt Q;
    struct StokesStore *stash;
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    switch (mmode) {
    case STOKES_MULT_A:
      err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,&du,NULL,&dv, NULL,NULL,NULL,NULL);dCHK(err);dCHK(err);
      for (dInt i=0; i<Q; i++) {
        dScalar Du[6],Dv[6],qq_unused[1];
        dTensorSymCompress3(du[i],Du);
        StokesPointwiseJacobian(&stash[i],jw[i],Du,0,Dv,qq_unused);
        dTensorSymUncompress3(Dv,dv[i]);
      }
      err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL,NULL,dv, NULL,NULL,NULL,NULL);dCHK(err);
      break;
    case STOKES_MULT_Bt:
      err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,NULL,NULL,&dv, &p,NULL,NULL,NULL);dCHK(err);dCHK(err);
      for (dInt i=0; i<Q; i++) {
        dScalar Dv[6];
        StokesPointwiseJacobian_Bt(jw[i],p[i],Dv);
        dTensorSymUncompress3(Dv,dv[i]);
      }
      err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL,NULL,dv, NULL,NULL,NULL,NULL);dCHK(err);
      break;
    case STOKES_MULT_B:
      err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,&du,NULL,NULL, NULL,NULL,&q,NULL);dCHK(err);dCHK(err);
      for (dInt i=0; i<Q; i++) {
        dScalar Du[6];
        dTensorSymCompress3(du[i],Du);
        StokesPointwiseJacobian_B(jw[i],Du,&q[i]); /* vv is pressure test function */
      }
      err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL,NULL,NULL, NULL,NULL,q,NULL);dCHK(err);
      break;
    default: dERROR(stk->comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid mmode");
    }
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = PetscLogEventEnd(LOG_StokesShellMult,A,gx,gy,gz);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesJacobianAssemble_Velocity(Stokes stk,Mat Ap,Vec Mdiag,Vec gx)
{
  dRulesetIterator iter;
  Vec Coords,gxu;
  dScalar *Kflat;
  dErr err;

  dFunctionBegin;
  err = VecZeroEntries(Mdiag);dCHK(err);
  err = StokesExtractGlobalSplit(stk,gx,&gxu,NULL);dCHK(err);
  err = dFSGetGeometryVectorExpanded(stk->fsu,&Coords);dCHK(err);
  err = StokesGetRegionIterator(stk,EVAL_JACOBIAN,&iter);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gxu,dFS_HOMOGENEOUS,Mdiag,dFS_HOMOGENEOUS, NULL,NULL);dCHK(err);
  err = dRulesetIteratorGetMatrixSpaceSplit(iter, NULL,NULL,NULL, NULL,&Kflat,NULL, NULL,NULL,NULL);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw,*interp_flat,*deriv_flat;
    const dInt *rowcol;
    dScalar (*x)[3],(*dx)[3][3],(*du)[9],(*v)[3];
    dInt Q,P;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,&du,&v,NULL, NULL,NULL,NULL,NULL);dCHK(err);dCHK(err);
    err = dRulesetIteratorGetPatchAssembly(iter, NULL,NULL,NULL,NULL, &P,&rowcol,&interp_flat,&deriv_flat, NULL,NULL,NULL,NULL);dCHK(err);
    {                           /* Scope so that we can declare new VLA pointers for convenient assembly */
      const dReal (*interp)[P] = (const dReal(*)[P])interp_flat;
      const dReal (*deriv)[P][3] = (const dReal(*)[P][3])deriv_flat;
      dScalar (*K)[3][P][3] = (dScalar(*)[3][P][3])Kflat;
      err = PetscMemzero(K,P*3*P*3*sizeof(K[0][0][0][0]));dCHK(err);
      for (dInt q=0; q<Q; q++) {
        struct StokesStore store;
        StokesPointwiseComputeStore(&stk->rheo,x[q],du[q],&store);
        for (dInt j=0; j<P; j++) { /* trial functions */
          for (dInt fj=0; fj<3; fj++) {
            dScalar uu[3] = {0},duu[3][3] = {{0},{0},{0}},dv[3][3],Dusym[6],Dvsym[6],q_unused;
            uu[fj] = interp[q][j];
            duu[fj][0] = deriv[q][j][0];
            duu[fj][1] = deriv[q][j][1];
            duu[fj][2] = deriv[q][j][2];
            dTensorSymCompress3(&duu[0][0],Dusym);
            StokesPointwiseJacobian(&store,jw[q],Dusym,0,Dvsym,&q_unused);
            dTensorSymUncompress3(Dvsym,&dv[0][0]);
            for (dInt i=0; i<P; i++) {
              for (dInt fi=0; fi<3; fi++) {
                K[i][fi][j][fj] += (+ deriv[q][i][0] * dv[fi][0]
                                    + deriv[q][i][1] * dv[fi][1]
                                    + deriv[q][i][2] * dv[fi][2]);
              }
            }
          }
        }
      }
      err = dFSMatSetValuesBlockedExpanded(stk->fsu,Ap,8,rowcol,8,rowcol,&K[0][0][0][0],ADD_VALUES);dCHK(err);
      for (dInt i=0; i<P; i++) {
        dScalar Mentry = 0;
        for (dInt q=0; q<Q; q++) Mentry += interp[q][i] * jw[q] * interp[q][i]; /* Integrate the diagonal entry over this element */
        v[i][0] += Mentry;
        v[i][1] += Mentry;
        v[i][2] += Mentry;
      }
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL,NULL,NULL, NULL,NULL,(dScalar**)&v,NULL, NULL,NULL,NULL,NULL);dCHK(err);
    err = dRulesetIteratorRestorePatchAssembly(iter, NULL,NULL,NULL,NULL, &P,&rowcol,&interp_flat,&deriv_flat, NULL,NULL,NULL,NULL);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesJacobianAssemble_Pressure(Stokes stk,Mat D,Mat Daux,Vec gx)
{
  dRulesetIterator iter;
  Vec              Coords,gxu;
  dScalar          *Kflat,*Kflat_aux;
  const dInt       *Ksizes;
  dErr             err;

  dFunctionBegin;
  /* It might seem weird to be getting velocity in the pressure assembly.  The reason is that this preconditioner
  * (indeed the entire problem) is always linear in pressure.  It \e might be nonlinear in velocity. */
  err = StokesExtractGlobalSplit(stk,gx,&gxu,NULL);dCHK(err);
  err = dFSGetGeometryVectorExpanded(stk->fsu,&Coords);dCHK(err);
  err = StokesGetRegionIterator(stk,EVAL_JACOBIAN,&iter);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gxu,dFS_INHOMOGENEOUS,NULL, NULL,NULL);dCHK(err);
  err = dRulesetIteratorGetMatrixSpaceSplit(iter, NULL,NULL,NULL, NULL,NULL,NULL, NULL,NULL,&Kflat);dCHK(err);
  err = dRulesetIteratorGetMatrixSpaceSizes(iter,NULL,NULL,&Ksizes);dCHK(err);
  err = dMallocA(Ksizes[8],&Kflat_aux);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw,*interp_flat,*deriv_flat;
    const dInt *rowcol;
    dScalar (*x)[3],(*dx)[3][3],(*du)[9];
    dInt Q,P;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,&du,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);dCHK(err);
    err = dRulesetIteratorGetPatchAssembly(iter, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL, &P,&rowcol,&interp_flat,&deriv_flat);dCHK(err);
    {
      const dReal (*interp)[P] = (const dReal(*)[P])interp_flat;
      const dReal (*deriv)[P][3] = (const dReal(*)[P][3])deriv_flat;
      dScalar (*K)[P] = (dScalar(*)[P])Kflat,(*Ka)[P] = (dScalar(*)[P])Kflat_aux;
      err = PetscMemzero(K,P*P*sizeof(K[0][0]));dCHK(err);
      err = PetscMemzero(Ka,P*P*sizeof(K[0][0]));dCHK(err);
      for (dInt q=0; q<Q; q++) {
        struct StokesStore store;
        StokesPointwiseComputeStore(&stk->rheo,x[q],du[q],&store);dCHK(err);
        for (dInt j=0; j<P; j++) { /* trial functions */
          for (dInt i=0; i<P; i++) {
            /* Scaled mass matrx */
            K[i][j] += interp[q][i] * jw[q] * (1./store.eta) * interp[q][j];
            /* Neumann Laplacian */
            Ka[i][j] += (+ deriv[q][i][0] * jw[q] * deriv[q][j][0]
                         + deriv[q][i][1] * jw[q] * deriv[q][j][1]
                         + deriv[q][i][2] * jw[q] * deriv[q][j][2]);
          }
        }
      }
      err = dFSMatSetValuesBlockedExpanded(stk->fsp,D,P,rowcol,P,rowcol,&K[0][0],ADD_VALUES);dCHK(err);
      if (Daux) {err = dFSMatSetValuesBlockedExpanded(stk->fsp,Daux,P,rowcol,P,rowcol,&Ka[0][0],ADD_VALUES);dCHK(err);}
    }
    err = dRulesetIteratorRestorePatchAssembly(iter, NULL,NULL,NULL,NULL, &P,&rowcol,&interp_flat,&deriv_flat, NULL,NULL,NULL,NULL);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = dFree(Kflat_aux);dCHK(err);
  dFunctionReturn(0);
}


static dErr StokesJacobian(SNES dUNUSED snes,Vec gx,Mat *J,Mat *Jp,MatStructure *structure,void *ctx)
{
  Stokes stk = ctx;
  dErr err;
  Mat A,D,Daux;
  Vec Mdiag;

  dFunctionBegin;
  err = MatNestGetSubMat(*Jp,0,0,&A);dCHK(err);
  err = MatNestGetSubMat(*Jp,1,1,&D);dCHK(err);
  err = PetscObjectQuery((dObject)D,"LSC_M_diag",(dObject*)&Mdiag);dCHK(err);
  err = PetscObjectQuery((dObject)D,"LSC_L",(dObject*)&Daux);dCHK(err);
  err = MatZeroEntries(*Jp);dCHK(err);
  if (Daux) {err = MatZeroEntries(Daux);dCHK(err);}
  err = StokesJacobianAssemble_Velocity(stk,A,Mdiag,gx);dCHK(err);
  err = StokesJacobianAssemble_Pressure(stk,D,Daux,gx);dCHK(err);
  if (Daux) {
    err = MatAssemblyBegin(Daux,MAT_FINAL_ASSEMBLY);dCHK(err);
    err = MatAssemblyEnd  (Daux,MAT_FINAL_ASSEMBLY);dCHK(err);
  }

  /* MatNest calls assembly on the constituent pieces */
  err = MatAssemblyBegin(*Jp,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyEnd(*Jp,MAT_FINAL_ASSEMBLY);dCHK(err);
  if (*J != *Jp) {
    err = MatAssemblyBegin(*J,MAT_FINAL_ASSEMBLY);dCHK(err);
    err = MatAssemblyEnd(*J,MAT_FINAL_ASSEMBLY);dCHK(err);
  }
  *structure = SAME_NONZERO_PATTERN;
  dFunctionReturn(0);
}

static dErr StokesErrorNorms(Stokes stk,Vec gx,dReal errorNorms[3],dReal gerrorNorms[3],dReal perrorNorms[3])
{
  dErr             err;
  Vec              Coords,gxu,gxp;
  dRulesetIterator iter;

  dFunctionBegin;
  err = dNormsStart(errorNorms,gerrorNorms);dCHK(err);
  err = dNormsStart(perrorNorms,NULL);dCHK(err);
  err = StokesExtractGlobalSplit(stk,gx,&gxu,&gxp);dCHK(err);
  err = StokesGetRegionIterator(stk,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(stk->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, gxu,dFS_INHOMOGENEOUS,NULL, gxp,dFS_INHOMOGENEOUS,NULL);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw;
    const dScalar (*x)[3],(*dx)[9],(*u)[3],(*du)[9],(*p)[1];
    dInt Q;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, (const dScalar**)&u,(const dScalar**)&du,NULL,NULL, (const dScalar**)&p,NULL,NULL,NULL);dCHK(err);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      dScalar uu[3],duu[9],pp[1],dpp[3];
      stk->exact.solution(&stk->exactctx,&stk->rheo,x[i],uu,duu,pp,dpp);
      err = dNormsUpdate(errorNorms,gerrorNorms,jw[i],3,uu,u[i],duu,du[i]);dCHK(err);
      err = dNormsUpdate(perrorNorms,NULL,jw[i],1,pp,p[i],NULL,NULL);dCHK(err);
    }
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = dNormsFinish(errorNorms,gerrorNorms);dCHK(err);
  err = dNormsFinish(perrorNorms,NULL);dCHK(err);
  dFunctionReturn(0);
}

static dErr StokesGetSolutionField_All(Stokes stk,dFS fs,dBool isvel,Vec *insoln)
{
  Vec      sol,xc,cvecg,cvec;
  dScalar *x;
  const dScalar *coords;
  dInt     n,bs;
  dErr     err;

  dFunctionBegin;
  *insoln = 0;
  err = dFSCreateGlobalVector(fs,&sol);dCHK(err);
  err = VecDohpGetClosure(sol,&xc);dCHK(err);
  err = dFSGetNodalCoordinatesGlobal(fs,&cvecg);dCHK(err);
  err = VecDohpGetClosure(cvecg,&cvec);dCHK(err);
  err = VecGetLocalSize(xc,&n);dCHK(err);
  err = VecGetBlockSize(xc,&bs);dCHK(err);
  {
    dInt nc;
    err = VecGetLocalSize(cvec,&nc);dCHK(err);
    if (nc*bs != n*3) dERROR(PETSC_COMM_SELF,1,"Coordinate vector has inconsistent size");
  }
  err = VecGetArray(xc,&x);dCHK(err);
  err = VecGetArrayRead(cvec,&coords);dCHK(err);
  for (dInt i=0; i<n/bs; i++) {
    dScalar u_unused[3],p_unused[1],du_unused[3*3],dp_unused[3];
    /* if \a isvel then \a x is the velocity field, otherwise it is the pressure field */
    stk->exact.solution(&stk->exactctx,&stk->rheo,&coords[3*i],isvel ? &x[i*bs] : u_unused,du_unused,isvel ? p_unused : &x[i*bs],dp_unused);
    /* printf("Node %3d: coords %+8f %+8f %+8f   exact %+8f %+8f %+8f\n",i,coords[3*i],coords[3*i+1],coords[3*i+2],x[3*i],x[3*i+1],x[3*i+2]); */
  }
  err = VecRestoreArray(xc,&x);dCHK(err);
  err = VecRestoreArrayRead(cvec,&coords);dCHK(err);
  err = VecDohpRestoreClosure(cvecg,&cvec);dCHK(err);
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

static dErr CheckNullSpace(SNES snes,Vec residual,dBool compute_explicit)
{
  Mat          mffd,J,Jp;
  dBool        isnull;
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
  if (!isnull) dERROR(PETSC_COMM_SELF,1,"Vector is not in the null space of the MFFD operator");dCHK(err);
  err = MatNullSpaceTest(matnull,J,&isnull);dCHK(err);
  if (!isnull) dERROR(PETSC_COMM_SELF,1,"Vector is not in the null space of J");dCHK(err);
  err = SNESComputeJacobian(snes,U,&J,&Jp,&mstruct);dCHK(err); /* To assemble blocks of Jp */
  err = MatNullSpaceTest(matnull,Jp,&isnull);dCHK(err);
  if (!isnull) dERROR(PETSC_COMM_SELF,1,"Vector is not in the null space of Jp");dCHK(err);
  err = MatNullSpaceDestroy(&matnull);dCHK(err);
  err = MatDestroy(mffd);dCHK(err);
  if (compute_explicit) {
    Mat expmat,expmat_fd;
    dInt m,n;
    dBool contour = dFALSE;
    err = MatGetLocalSize(J,&m,&n);dCHK(err);
    err = MatComputeExplicitOperator(J,&expmat);dCHK(err);
    err = MatDuplicate(expmat,MAT_DO_NOT_COPY_VALUES,&expmat_fd);dCHK(err);
    err = SNESDefaultComputeJacobian(snes,U,&expmat_fd,&expmat_fd,&mstruct,NULL);dCHK(err);
    err = MatSetOptionsPrefix(expmat,"explicit_");dCHK(err);
    err = MatSetOptionsPrefix(expmat_fd,"explicit_fd_");dCHK(err);
    err = MatSetFromOptions(expmat);dCHK(err);
    err = MatSetFromOptions(expmat_fd);dCHK(err);

    err = PetscOptionsGetBool(NULL,"-mat_view_contour",&contour,NULL);dCHK(err);
    if (contour) {err = PetscViewerPushFormat(PETSC_VIEWER_DRAW_WORLD,PETSC_VIEWER_DRAW_CONTOUR);dCHK(err);}
    {
      dBool flg = dFALSE;
      err = PetscOptionsGetBool(NULL,"-explicit_mat_view",&flg,NULL);dCHK(err);
      if (flg) {
        err = PetscViewerASCIIPrintf(PETSC_VIEWER_STDOUT_WORLD,"###  Explicit matrix using mat-free implementation of J\n");dCHK(err);
        err = MatView(expmat,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
      }
      flg = dFALSE;
      err = PetscOptionsGetBool(NULL,"-explicit_mat_view_draw",&flg,NULL);dCHK(err);
      if (flg) {err = MatView(expmat,PETSC_VIEWER_DRAW_WORLD);dCHK(err);}
    }

    {
      dBool flg = dFALSE;
      err = PetscOptionsGetBool(NULL,"-explicit_fd_mat_view",&flg,NULL);dCHK(err);
      if (flg) {
        err = PetscViewerASCIIPrintf(PETSC_VIEWER_STDOUT_WORLD,"###  Explicit matrix using FD\n");dCHK(err);
        err = MatView(expmat_fd,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
      }
      flg = dFALSE;
      err = PetscOptionsGetBool(NULL,"-explicit_fd_mat_view_draw",&flg,NULL);dCHK(err);
      if (flg) {err = MatView(expmat_fd,PETSC_VIEWER_DRAW_WORLD);dCHK(err);}
    }

    err = MatAXPY(expmat,-1,expmat_fd,SAME_NONZERO_PATTERN);dCHK(err);
    {
      dBool flg = dFALSE;
      err = PetscOptionsGetBool(NULL,"-explicit_diff_mat_view",&flg,NULL);dCHK(err);
      if (flg) {
        err = PetscViewerASCIIPrintf(PETSC_VIEWER_STDOUT_WORLD,"###  Difference between mat-free implementation of J and FD\n");dCHK(err);
        err = MatView(expmat,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
      }
      flg = dFALSE;
      err = PetscOptionsGetBool(NULL,"-explicit_diff_mat_view_draw",&flg,NULL);dCHK(err);
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
  dBool nocheck,check_null,compute_explicit,use_jblock;
  dErr err;

  err = dInitialize(&argc,&argv,NULL,help);dCHK(err);
  comm = PETSC_COMM_WORLD;
  viewer = PETSC_VIEWER_STDOUT_WORLD;
  err = PetscLogEventRegister("StokesShellMult",MAT_CLASSID,&LOG_StokesShellMult);dCHK(err);

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
      err = MatGetColoring(Jp,MATCOLORINGID,&iscolor);dCHK(err);
      err = MatFDColoringCreate(Jp,iscolor,&fdcolor);dCHK(err);
      err = ISColoringDestroy(iscolor);dCHK(err);
      err = MatFDColoringSetFunction(fdcolor,(PetscErrorCode(*)(void))StokesFunction,stk);dCHK(err);
      err = MatFDColoringSetFromOptions(fdcolor);dCHK(err);
      err = SNESSetJacobian(snes,J,Jp,SNESDefaultComputeJacobianColor,fdcolor);dCHK(err);
    } break;
    case 3:
      err = SNESSetJacobian(snes,J,Jp,StokesJacobian,stk);dCHK(err);
      break;
    default: dERROR(PETSC_COMM_SELF,1,"Not supported");
  }
  err = SNESSetFromOptions(snes);dCHK(err);
  {
    KSP    ksp;
    PC     pc;

    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPGetPC(ksp,&pc);dCHK(err);
    err = PCFieldSplitSetIS(pc,"u",stk->ublock);dCHK(err);
    err = PCFieldSplitSetIS(pc,"p",stk->pblock);dCHK(err);
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
    err = MatNullSpaceDestroy(&matnull);dCHK(err);
  }
  if (check_null) {
    err = CheckNullSpace(snes,r,compute_explicit);dCHK(err);
  }
  err = VecZeroEntries(r);dCHK(err);
  err = VecZeroEntries(x);dCHK(err);
  err = SNESSolve(snes,NULL,x);dCHK(err); /* ###  SOLVE  ### */
  if (1) {
    MatNullSpace matnull;
    KSP ksp;
    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPGetNullSpace(ksp,&matnull);dCHK(err); /* does not reference */
    err = MatNullSpaceRemove(matnull,x,NULL);dCHK(err);
  }
  if (!nocheck) {
    dReal anorm[2],anorminf,inorm[3],enorm[3],gnorm[3],epnorm[3];
    err = StokesErrorNorms(stk,x,enorm,gnorm,epnorm);dCHK(err);
    err = VecNorm(r,NORM_1_AND_2,anorm);dCHK(err);
    err = VecNorm(r,NORM_INFINITY,&anorminf);dCHK(err);
    err = VecWAXPY(r,-1,soln,x);dCHK(err);
    err = VecNorm(r,NORM_1_AND_2,inorm);dCHK(err);
    err = VecNorm(r,NORM_INFINITY,&inorm[2]);dCHK(err);
    err = dPrintf(comm,"Algebraic residual        |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",anorm[0],anorm[1],anorminf);dCHK(err);
    err = dPrintf(comm,"Interpolation residual    |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",inorm[0],inorm[1],inorm[2]);dCHK(err);
    err = dPrintf(comm,"Pointwise solution error  |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",enorm[0],enorm[1],enorm[2]);dCHK(err);
    err = dPrintf(comm,"Pointwise gradient error  |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",gnorm[0],gnorm[1],gnorm[2]);dCHK(err);
  }

  err = VecDestroy(r);dCHK(err);
  err = VecDestroy(x);dCHK(err);
  err = VecDestroy(soln);dCHK(err);
  err = SNESDestroy(snes);dCHK(err);
  if (fdcolor) {err = MatFDColoringDestroy(fdcolor);dCHK(err);}
  if (J != Jp) {err = MatDestroy(J);dCHK(err);}
  err = MatDestroy(Jp);dCHK(err);
  err = StokesDestroy(stk);dCHK(err);
  err = dFinalize();dCHK(err);
  return 0;
}
