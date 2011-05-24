static const char help[] = "Solve viscous flow coupled to a heat transport problem using dual order elements.\n"
  "The model problem is\n"
  "  -div(eta Du) + grad(p) = f\n"
  "                  div(u) = g\n"
  "    div(u T) - eta Du:Du = h\n"
  "where\n"
  "  D is the symmetric gradient operator\n"
  "  eta(gamma,T) = B(T) (0.5*eps^2 + gamma)^{(p-2)/2}\n"
  "  gamma = Du : Du/2\n"
  "  B(T) = B_0 exp(Q/(n R T))\n"
  "The weak form is\n"
  "  int_Omega eta Dv:Du - p div(v) - q div(u) - v.f - q g -  = 0\n"
  "with Jacobian\n"
  "  int_Omega eta Dv:Du + eta' (Dv:Dw)(Dw:Du) - p div(v) - q div(u) = 0\n"
  "The problem is linear for p=2, an incompressible for g=0\n\n";

#include <petscts.h>
#include <dohpstring.h>
#include <dohpviewer.h>

#include "vhtimpl.h"

PetscFList VHTCaseList = NULL;

#define VHTCaseType char*

dErr VHTCaseRegister(const char *name,VHTCaseCreateFunction screate)
{
  dErr err;
  dFunctionBegin;
  err = PetscFListAdd(&VHTCaseList,name,"",(void(*)(void))screate);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTCaseFind(const char *name,VHTCaseCreateFunction *screate)
{
  dErr err;

  dFunctionBegin;
  err = PetscFListFind(VHTCaseList,PETSC_COMM_WORLD,name,PETSC_FALSE,(void(**)(void))screate);dCHK(err);
  if (!*screate) dERROR(PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"VHT Case \"%s\" could not be found",name);
  dFunctionReturn(0);
}
static dErr VHTCaseSetType(VHTCase scase,const VHTCaseType type)
{
  dErr err;
  VHTCaseCreateFunction f;

  dFunctionBegin;
  err = VHTCaseFind(type,&f);dCHK(err);
  err = (*f)(scase);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTCaseUpdateUnitsTable(VHTCase scase)
{
  struct VHTUnitTable *u = &scase->utable;
  dUnits units = scase->units;
  dErr err;

  dFunctionBegin;
  err = dUnitsCreateUnit(units,"DENSITY",NULL,NULL,4,(dReal[4]){[dUNITS_LENGTH]=-3.0,[dUNITS_MASS]=1.0},&u->Density);dCHK(err);
  err = dUnitsCreateUnit(units,"ENERGY",NULL,NULL,4,(dReal[4]){[dUNITS_LENGTH]=2.0,[dUNITS_MASS]=1.0,[dUNITS_TIME]=-2.0},&u->Energy);dCHK(err);
  err = dUnitsCreateUnit(units,"PRESSURE",NULL,NULL,4,(dReal[4]){[dUNITS_LENGTH]=-1.0,[dUNITS_MASS]=1.0,[dUNITS_TIME]=-2.0},&u->Pressure);dCHK(err);
  err = dUnitsCreateUnit(units,"STRAINRATE",NULL,NULL,4,(dReal[4]){[dUNITS_TIME]=-1.0},&u->StrainRate);dCHK(err);
  err = dUnitsCreateUnit(units,"VELOCITY",NULL,NULL,4,(dReal[4]){[dUNITS_LENGTH]=1.0,[dUNITS_TIME]=-1.0},&u->Velocity);dCHK(err);
  err = dUnitsCreateUnit(units,"VISCOSITY",NULL,NULL,4,(dReal[4]){[dUNITS_LENGTH]=-1.0,[dUNITS_MASS]=1.0,[dUNITS_TIME]=-1.0},&u->Viscosity);dCHK(err);
  err = dUnitsCreateUnit(units,"VOLUME",NULL,NULL,4,(dReal[4]){[dUNITS_LENGTH]=3.0},&u->Volume);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTCaseProfile_Default(VHTCase scase)
{
  struct VHTRheology *rheo = &scase->rheo;
  dFunctionBegin;
  rheo->B0           = 1;
  rheo->Bomega       = 1;
  rheo->R            = 1;
  rheo->Q            = 1;
  rheo->V            = 0;
  rheo->du0          = 1;
  rheo->eps          = 1;
  rheo->pe           = 2;
  rheo->k_T          = 1;
  rheo->kappa_w      = 0.5;
  rheo->c_i          = 1;
  rheo->Latent       = 1;
  rheo->rhoi         = 1;
  rheo->rhow         = 2;
  rheo->beta_CC      = 0.1;
  rheo->T0           = 5;
  rheo->T3           = 10;
  rheo->splice_delta = 1;
  dFunctionReturn(0);
}
static dErr VHTCaseProfile_Ice(VHTCase scase)
{
  const struct VHTUnitTable *u = &scase->utable;
  struct VHTRheology *rheo = &scase->rheo;
  const dReal
    n = 3,
    Asoftness_si = 3.61e-13, // Softness parameter
    refstrainrate_si = 1e-10; // about 0.003 / year

  dFunctionBegin;
  // Viscosity at reference strain rate before dimensionless Arrhenius term
  rheo->B0           = dUnitNonDimensionalizeSI(u->Viscosity,pow(Asoftness_si,-1/n) * pow(0.5*dSqr(refstrainrate_si),(1-n)/(2*n)));
  rheo->Bomega       = 181.25;  // nondimensional
  rheo->R            = dUnitNonDimensionalizeSI(u->Energy,8.314) / dUnitNonDimensionalizeSI(u->Temperature,1.0); // 8.314 J mol^-1 K^-1
  rheo->Q            = dUnitNonDimensionalizeSI(u->Energy,6.0e4); // 6.0 J mol^-1
  rheo->V            = dUnitNonDimensionalizeSI(u->Volume,-13.0e-6); // m^3 / mol; it always bothered my that the "volume" was negative
  rheo->du0          = dUnitNonDimensionalizeSI(u->StrainRate,refstrainrate_si); // Reference strain rate
  rheo->gamma0       = 0.5*dSqr(rheo->du0); // second invariant of reference strain rate
  rheo->eps          = 1e-3;    // Dimensionless fraction of du0
  rheo->pe           = 1 + 1./n;
  rheo->k_T          = dUnitNonDimensionalizeSI(u->Energy,2.1) / (dUnitNonDimensionalizeSI(u->Time,1)*dUnitNonDimensionalizeSI(u->Temperature,1)*dUnitNonDimensionalizeSI(u->Length,1)); // thermal conductivity (W/(m K))
  rheo->kappa_w      = dUnitNonDimensionalizeSI(u->Mass,1.045e-4) / (dUnitDimensionalizeSI(u->Length,1)*dUnitNonDimensionalizeSI(u->Time,1));
  rheo->c_i          = dUnitNonDimensionalizeSI(u->Energy,2009) / (dUnitDimensionalizeSI(u->Mass,1)*dUnitNonDimensionalizeSI(u->Temperature,1));
  rheo->Latent       = dUnitNonDimensionalizeSI(u->Energy,3.34e5) / dUnitDimensionalizeSI(u->Mass,1);
  rheo->rhoi         = dUnitNonDimensionalizeSI(u->Density,910); // Density of ice
  rheo->rhow         = dUnitNonDimensionalizeSI(u->Density,999.8395); // Density of water at 0 degrees C, STP
  rheo->beta_CC      = dUnitNonDimensionalizeSI(u->Temperature,7.9e-8) / dUnitNonDimensionalizeSI(u->Pressure,1.0);
  rheo->T0           = dUnitNonDimensionalizeSI(u->Temperature,260.);
  rheo->T3           = dUnitNonDimensionalizeSI(u->Temperature,273.15);
  rheo->splice_delta = 1e-3 * rheo->Latent;
  dFunctionReturn(0);
}
static dErr VHTCaseSetFromOptions(VHTCase scase)
{
  struct VHTRheology *rheo = &scase->rheo;
  PetscFList profiles = NULL;
  char prof[256] = "default";
  dErr (*rprof)(VHTCase);
  dErr err;

  dFunctionBegin;
  err = dUnitsSetFromOptions(scase->units);dCHK(err);
  err = VHTCaseUpdateUnitsTable(scase);dCHK(err);
  err = PetscFListAdd(&profiles,"default",NULL,(void(*)(void))VHTCaseProfile_Default);dCHK(err);
  err = PetscFListAdd(&profiles,"ice",NULL,(void(*)(void))VHTCaseProfile_Ice);dCHK(err);
  err = PetscOptionsBegin(scase->comm,NULL,"VHTCase options",__FILE__);dCHK(err); {
    err = PetscOptionsList("-rheo_profile","Rheological profile",NULL,profiles,prof,prof,sizeof prof,NULL);dCHK(err);
    err = PetscFListFind(profiles,scase->comm,prof,PETSC_FALSE,(void(**)(void))&rprof);dCHK(err);
    err = (*rprof)(scase);dCHK(err);
    err = PetscFListDestroy(&profiles);dCHK(err);
    err = PetscOptionsReal("-rheo_B0","Viscosity at reference strain rate and temperature","",rheo->B0,&rheo->B0,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_Bomega","Softening due to water content","",rheo->Bomega,&rheo->Bomega,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_R","Ideal gas constant","",rheo->R,&rheo->R,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_Q","Activation Energy","",rheo->Q,&rheo->Q,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_V","Activation Volume","",rheo->V,&rheo->V,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_du0","Regularization (rheology)","",rheo->du0,&rheo->du0,NULL);dCHK(err);
    rheo->gamma0 = 0.5*dSqr(rheo->du0);
    err = PetscOptionsReal("-rheo_eps","Nondimensional regularization (rheology)","",rheo->eps,&rheo->eps,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_p","Power p=1+1/n where n is Glen exponent","",rheo->pe,&rheo->pe,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_k_T","Thermal conductivity in the cold part","",rheo->k_T,&rheo->k_T,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_kappa_w","Hydraulic conductivity in the warm part","",rheo->kappa_w,&rheo->kappa_w,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_c_i","Specific heat capacity of cold part","",rheo->c_i,&rheo->c_i,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_Latent","Latent heat of fusion","",rheo->Latent,&rheo->Latent,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_rhoi","Density of cold part","",rheo->rhoi,&rheo->rhoi,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_rhow","Density of melted part","",rheo->rhow,&rheo->rhow,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_beta_CC","Clausius-Clapeyron gradient","",rheo->beta_CC,&rheo->beta_CC,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_T0","Reference temperature (corresponds to enthalpy=0)","",rheo->T0,&rheo->T0,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_T3","Triple point temperature","",rheo->T3,&rheo->T3,NULL);dCHK(err);
    err = PetscOptionsReal("-rheo_splice_delta","Characteristic width of split","",rheo->splice_delta,&rheo->splice_delta,NULL);dCHK(err);
    err = PetscOptionsReal("-gravity","Nondimensional gravitational force","",scase->gravity,&scase->gravity,NULL);dCHK(err);
    if (scase->setfromoptions) {err = (*scase->setfromoptions)(scase);dCHK(err);}
  } err = PetscOptionsEnd();dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTCaseDestroy(VHTCase *scase)
{
  dErr err;

  dFunctionBegin;
  if ((*scase)->destroy) {err = ((*scase)->destroy)(*scase);dCHK(err);}
  err = dFree(*scase);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTCaseRegisterAll(void)
{
  dErr err;

  dFunctionBegin;
  err = VHTCaseRegisterAll_Exact();dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTLogEpochView(struct VHTLogEpoch *ep,PetscViewer viewer,const char *fmt,...)
{
  va_list Argp;
  char name[4096];
  size_t fullLen;
  dErr err;

  dFunctionBegin;
  va_start(Argp,fmt);
  err = PetscVSNPrintf(name,sizeof name,fmt,&fullLen,Argp);dCHK(err);
  va_end(Argp);
  err = PetscViewerASCIIPrintf(viewer,"%s: eta [%8.2e,%8.2e]  cPeclet [%8.2e,%8.2e]\n",name,ep->eta[0],ep->eta[1],ep->cPeclet[0],ep->cPeclet[1]);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTLogView(struct VHTLog *vlog,PetscViewer viewer)
{
  dErr err;

  dFunctionBegin;
  err = PetscViewerASCIIPrintf(viewer,"Logged %d epochs\n",vlog->epoch+1);dCHK(err);
  err = VHTLogEpochView(&vlog->global,viewer,"Global");dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTLogEpochReset(struct VHTLogEpoch *ep)
{
  dFunctionBegin;
  ep->eta[0] = PETSC_MAX_REAL;
  ep->eta[1] = PETSC_MIN_REAL;
  ep->cPeclet[0] = PETSC_MAX_REAL;
  ep->cPeclet[1] = PETSC_MIN_REAL;
  dFunctionReturn(0);
}
static dErr VHTLogEpochStart(struct VHTLog *vlog)
{
  dErr err;

  dFunctionBegin;
  vlog->epoch++;
  if (vlog->epoch >= vlog->alloc) {
    dInt newalloc = vlog->alloc * 2 + 16;
    struct VHTLogEpoch *tmp = vlog->epochs;
    err = dCallocA(newalloc,&vlog->epochs);dCHK(err);
    err = dMemcpy(vlog->epochs,tmp,vlog->alloc*sizeof(tmp[0]));dCHK(err);
    err = dFree(tmp);dCHK(err);dCHK(err);
    vlog->alloc = newalloc;
  }
  err = VHTLogEpochReset(&vlog->epochs[vlog->epoch]);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTLogEpochEnd(struct VHTLog *vlog)
{
  struct VHTLogEpoch *g = &vlog->global,*e = &vlog->epochs[vlog->epoch];
  dErr err;

  dFunctionBegin;
  g->cPeclet[0] = dMin(g->cPeclet[0],e->cPeclet[0]);
  g->cPeclet[1] = dMax(g->cPeclet[1],e->cPeclet[1]);
  g->eta[0]     = dMin(g->eta[0],e->eta[0]);
  g->eta[1]     = dMax(g->eta[1],e->eta[1]);
  if (vlog->monitor) {err = VHTLogEpochView(e,PETSC_VIEWER_STDOUT_WORLD,"Epoch[%d]",vlog->epoch);dCHK(err);}
  dFunctionReturn(0);
}
static void VHTLogStash(struct VHTLog *vlog,struct VHTRheology *rheo,const dReal dx[9],const struct VHTStash *stash)
{
  struct VHTLogEpoch *ep = &vlog->epochs[vlog->epoch];
  const dReal *u = stash->u;
  dReal kappa,cPeclet,uh2 = 0;
  for (dInt i=0; i<3; i++) uh2 += dSqr(dx[i*3+0]*u[0] + dx[i*3+1]*u[1] + dx[i*3+2]*u[2]);
  kappa = rheo->k_T*stash->T1E + rheo->Latent*rheo->kappa_w*stash->omega1E;
  cPeclet = dSqrt(uh2) / kappa;
  ep->cPeclet[0] = dMin(ep->cPeclet[0],cPeclet);
  ep->cPeclet[1] = dMax(ep->cPeclet[1],cPeclet);
  ep->eta[0]     = dMin(ep->eta[0],stash->eta);
  ep->eta[1]     = dMax(ep->eta[1],stash->eta);
}
static dErr VHTLogSetFromOptions(struct VHTLog *vlog)
{
  dErr err;

  dFunctionBegin;
  err = PetscOptionsBool("-vht_log_monitor","View each epoch",NULL,vlog->monitor,&vlog->monitor,NULL);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTLogReset(struct VHTLog *vlog)
{
  dFunctionBegin;
  vlog->epoch = 0;
  dFunctionReturn(0);
}

#define ALEN(a) ((dInt)(sizeof(a)/sizeof(a)[0]))
static PetscLogEvent LOG_VHTShellMult;

static dErr VHTGetNullSpace(VHT vht,MatNullSpace *matnull);
static dErr MatMultXIorA_VHT_stokes(Mat A,Vec gx,Vec gy,Vec gz,InsertMode,VHTMultMode);
static dErr MatMult_Nest_VHT_all(Mat J,Vec gx,Vec gy);
static dErr MatMult_VHT_uu(Mat A,Vec gx,Vec gy) {return MatMultXIorA_VHT_stokes(A,gx,gy,NULL,INSERT_VALUES,VHT_MULT_UU);}
static dErr MatMult_VHT_up(Mat A,Vec gx,Vec gy) {return MatMultXIorA_VHT_stokes(A,gx,gy,NULL,INSERT_VALUES,VHT_MULT_UP);}
static dErr MatMult_VHT_pu(Mat A,Vec gx,Vec gy) {return MatMultXIorA_VHT_stokes(A,gx,gy,NULL,INSERT_VALUES,VHT_MULT_PU);}
static dErr MatMultAdd_VHT_uu(Mat A,Vec gx,Vec gy,Vec gz) {return MatMultXIorA_VHT_stokes(A,gx,gy,gz,ADD_VALUES,VHT_MULT_UU);}
static dErr MatMultAdd_VHT_up(Mat A,Vec gx,Vec gy,Vec gz) {return MatMultXIorA_VHT_stokes(A,gx,gy,gz,ADD_VALUES,VHT_MULT_UP);}
static dErr MatMultAdd_VHT_pu(Mat A,Vec gx,Vec gy,Vec gz) {return MatMultXIorA_VHT_stokes(A,gx,gy,gz,ADD_VALUES,VHT_MULT_PU);}
static dErr MatMult_VHT_ee(Mat A,Vec gx,Vec gy);
static dErr MatGetVecs_VHT_stokes(Mat,Vec*,Vec*);
static dErr MatGetVecs_VHT_ee(Mat,Vec*,Vec*);

static dErr VHTCreate(MPI_Comm comm,VHT *invht)
{
  VHT vht;
  dErr err;

  dFunctionBegin;
  *invht = 0;
  err = dNew(struct _n_VHT,&vht);dCHK(err);
  vht->comm = comm;

  vht->velocityBDeg  = 3;
  vht->pressureCodim = 1;
  vht->enthalpyBDeg  = 3;
  vht->dirichlet[0]  = 100;
  vht->dirichlet[1]  = 200;
  vht->dirichlet[2]  = 300;
  vht->alldirichlet  = dTRUE;
  vht->function_qmethod = dQUADRATURE_METHOD_FAST;
  vht->jacobian_qmethod = dQUADRATURE_METHOD_SPARSE;

  err = dCalloc(sizeof(*vht->scase),&vht->scase);dCHK(err);

  vht->log.epoch = -1;
  err = VHTLogEpochReset(&vht->log.global);dCHK(err);

  err = dUnitsCreate(vht->comm,&vht->scase->units);dCHK(err);
  {
    
    dUnits units = vht->scase->units;
    struct VHTUnitTable *u = &vht->scase->utable;
    err = dUnitsSetBase(units,dUNITS_LENGTH,"metre","m",1,100,&u->Length);dCHK(err);
    err = dUnitsSetBase(units,dUNITS_TIME,"year","a",31556926,1,&u->Time);dCHK(err);
    err = dUnitsSetBase(units,dUNITS_MASS,"exaton","Et",1e21,1000,&u->Mass);dCHK(err);
    err = dUnitsSetBase(units,dUNITS_TEMPERATURE,"Kelvin","K",1,1,&u->Temperature);dCHK(err);
  }

  *invht = vht;
  dFunctionReturn(0);
}

static dErr MatGetVecs_VHT_stokes(Mat A,Vec *x,Vec *y)
{
  VHT vht;
  dInt m,n,nu,np;
  dErr err;

  dFunctionBegin;
  err = MatShellGetContext(A,(void**)&vht);dCHK(err);
  err = MatGetLocalSize(A,&m,&n);dCHK(err);
  err = VecGetLocalSize(vht->gvelocity,&nu);dCHK(err);
  err = VecGetLocalSize(vht->gpressure,&np);dCHK(err);
  if (nu==np) dERROR(PETSC_COMM_SELF,1,"Degenerate case, don't know which space to copy");
  if (x) {
    if (n == nu) {
      err = VecDuplicate(vht->gvelocity,x);dCHK(err);
    } else if (n == np) {
      err = VecDuplicate(vht->gpressure,x);dCHK(err);
    } else dERROR(PETSC_COMM_SELF,1,"sizes do not agree with either space");
  }
  if (y) {
    if (n == nu) {
      err = VecDuplicate(vht->gvelocity,y);dCHK(err);
    } else if (n == np) {
      err = VecDuplicate(vht->gpressure,y);dCHK(err);
    } else dERROR(PETSC_COMM_SELF,1,"sizes do not agree with either space");
  }
  dFunctionReturn(0);
}

static dErr MatGetVecs_VHT_ee(Mat A,Vec *x,Vec *y)
{
  VHT vht;
  dErr err;

  dFunctionBegin;
  err = MatShellGetContext(A,(void**)&vht);dCHK(err);
  if (x) {err = VecDuplicate(vht->genthalpy,x);dCHK(err);}
  if (y) {err = VecDuplicate(vht->genthalpy,y);dCHK(err);}
  dFunctionReturn(0);
}

static dErr VHTSetFromOptions(VHT vht)
{
  char scasename[256] = "Exact0";
  dMesh mesh;
  dFS fsu,fsp,fse;
  dJacobi jac;
  dMeshESH domain;
  dMeshTag dutag,dptag,detag;
  dErr err;

  dFunctionBegin;
  err = dStrcpyS(vht->mattype_Buu,sizeof(vht->mattype_Buu),MATBAIJ);dCHK(err);
  err = dStrcpyS(vht->mattype_Bpp,sizeof(vht->mattype_Bpp),MATAIJ);dCHK(err);
  err = dStrcpyS(vht->mattype_Bee,sizeof(vht->mattype_Bee),MATAIJ);dCHK(err);
  err = PetscOptionsBegin(vht->comm,NULL,"Viscous Heat Transport options",__FILE__);dCHK(err); {
    err = PetscOptionsInt("-vht_u_bdeg","Constant isotropic degree to use for velocity","",vht->velocityBDeg,&vht->velocityBDeg,NULL);dCHK(err);
    err = PetscOptionsInt("-vht_p_codim","Reduce pressure space by this factor","",vht->pressureCodim,&vht->pressureCodim,NULL);dCHK(err);
    err = PetscOptionsInt("-vht_e_bdeg","Constant isotropic degree to use for enthalpy","",vht->enthalpyBDeg,&vht->enthalpyBDeg,NULL);dCHK(err);
    err = PetscOptionsBool("-vht_cardinal_mass","Assemble diagonal mass matrix","",vht->cardinalMass,&vht->cardinalMass,NULL);dCHK(err);
    err = PetscOptionsList("-vht_Buu_mat_type","Matrix type for velocity-velocity operator","",MatList,vht->mattype_Buu,vht->mattype_Buu,sizeof(vht->mattype_Buu),NULL);dCHK(err);
    err = PetscOptionsList("-vht_Bpp_mat_type","Matrix type for pressure-pressure operator","",MatList,vht->mattype_Bpp,vht->mattype_Bpp,sizeof(vht->mattype_Bpp),NULL);dCHK(err);
    err = PetscOptionsList("-vht_Bee_mat_type","Matrix type for enthalpy-enthalpy operator","",MatList,vht->mattype_Bee,vht->mattype_Bee,sizeof(vht->mattype_Bee),NULL);dCHK(err);
    err = PetscOptionsEnum("-vht_f_qmethod","Quadrature method for residual evaluation/matrix-free","",dQuadratureMethods,(PetscEnum)vht->function_qmethod,(PetscEnum*)&vht->function_qmethod,NULL);dCHK(err);
    err = PetscOptionsEnum("-vht_jac_qmethod","Quadrature to use for Jacobian assembly","",dQuadratureMethods,(PetscEnum)vht->jacobian_qmethod,(PetscEnum*)&vht->jacobian_qmethod,NULL);dCHK(err);
    {
      dBool flg; dInt n = ALEN(vht->dirichlet);
      err = PetscOptionsIntArray("-dirichlet","List of boundary sets on which to impose Dirichlet conditions","",vht->dirichlet,&n,&flg);dCHK(err);
      if (flg) {
        for (dInt i=n; i<ALEN(vht->dirichlet); i++) vht->dirichlet[i] = 0; /* Clear out any leftover values */
        if (n < 3) vht->alldirichlet = dFALSE;                             /* @bug More work to determine independent of the mesh whether all the boundaries are Dirichlet */
      }
    }
    err = PetscOptionsList("-vht_case","Which sort of case to run","",VHTCaseList,scasename,scasename,sizeof(scasename),NULL);dCHK(err);
    err = VHTLogSetFromOptions(&vht->log);dCHK(err);
  } err = PetscOptionsEnd();dCHK(err);

  err = dMeshCreate(vht->comm,&mesh);dCHK(err);
  err = dMeshSetInFile(mesh,"dblock.h5m",NULL);dCHK(err);
  err = dMeshSetFromOptions(mesh);dCHK(err);
  err = dMeshLoad(mesh);dCHK(err);
  err = dMeshGetRoot(mesh,&domain);dCHK(err); /* Need a taggable set */
  err = dMeshSetDuplicateEntsOnly(mesh,domain,&domain);dCHK(err);
  err = PetscObjectSetName((PetscObject)mesh,"dMesh_0");dCHK(err);

  err = dJacobiCreate(vht->comm,&jac);dCHK(err);
  err = dJacobiSetFromOptions(jac);dCHK(err);

  err = dMeshCreateRuleTagIsotropic(mesh,domain,"vht_efs_velocity_degree",vht->velocityBDeg,&dutag);dCHK(err);
  err = dMeshCreateRuleTagIsotropic(mesh,domain,"vht_efs_pressure_degree",vht->velocityBDeg-vht->pressureCodim,&dptag);dCHK(err);
  err = dMeshCreateRuleTagIsotropic(mesh,domain,"vht_efs_enthalpy_degree",vht->enthalpyBDeg,&detag);dCHK(err);

  err = dFSCreate(vht->comm,&fsu);dCHK(err);
  err = dFSSetBlockSize(fsu,3);dCHK(err);
  err = dFSSetMesh(fsu,mesh,domain);dCHK(err);
  err = dFSSetDegree(fsu,jac,dutag);dCHK(err);
  for (dInt i=0; i<ALEN(vht->dirichlet) && vht->dirichlet[i]>0; i++) {
    err = dFSRegisterBoundary(fsu,vht->dirichlet[i],dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  }
  err = PetscObjectSetOptionsPrefix((dObject)fsu,"u");dCHK(err);
  err = dFSSetFromOptions(fsu);dCHK(err);
  err = PetscObjectSetName((PetscObject)fsu,"dFS_U_0");dCHK(err);
  vht->fsu = fsu;

  err = dFSCreate(vht->comm,&fsp);dCHK(err);
  err = dFSSetMesh(fsp,mesh,domain);dCHK(err);
  err = dFSSetDegree(fsp,jac,dptag);dCHK(err);
  err = PetscObjectSetOptionsPrefix((dObject)fsp,"p");dCHK(err);
  /* No boundaries, the pressure space has Neumann conditions when Dirichlet velocity conditions are applied */
  err = dFSSetFromOptions(fsp);dCHK(err);
  err = PetscObjectSetName((PetscObject)fsp,"dFS_P_0");dCHK(err);
  vht->fsp = fsp;

  err = dFSCreate(vht->comm,&fse);dCHK(err);
  err = dFSSetMesh(fse,mesh,domain);dCHK(err);
  err = dFSSetDegree(fse,jac,detag);dCHK(err);
  err = dFSRegisterBoundary(fse,100,dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  err = dFSRegisterBoundary(fse,200,dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  err = dFSRegisterBoundary(fse,300,dFSBSTATUS_DIRICHLET,NULL,NULL);dCHK(err);
  err = PetscObjectSetOptionsPrefix((dObject)fse,"e");dCHK(err);
  err = dFSSetFromOptions(fse);dCHK(err);
  err = PetscObjectSetName((PetscObject)fse,"dFS_E_0");dCHK(err);
  vht->fse = fse;

  err = dFSCreateExpandedVector(fsu,&vht->xu);dCHK(err);
  err = VecDuplicate(vht->xu,&vht->yu);dCHK(err);

  err = dFSCreateExpandedVector(fsp,&vht->xp);dCHK(err);
  err = VecDuplicate(vht->xp,&vht->yp);dCHK(err);

  err = dFSCreateExpandedVector(fsu,&vht->xe);dCHK(err);
  err = VecDuplicate(vht->xe,&vht->ye);dCHK(err);

  {
    dInt nu,np,ne,nul,npl,nel;
    err = dFSCreateGlobalVector(vht->fsu,&vht->gvelocity);dCHK(err);
    err = dFSCreateGlobalVector(vht->fsp,&vht->gpressure);dCHK(err);
    err = dFSCreateGlobalVector(vht->fse,&vht->genthalpy);dCHK(err);
    err = PetscObjectSetName((PetscObject)vht->gvelocity,"Velocity");dCHK(err);
    err = PetscObjectSetName((PetscObject)vht->gpressure,"Pressure");dCHK(err);
    err = PetscObjectSetName((PetscObject)vht->genthalpy,"Enthalpy");dCHK(err);
    err = VecGetLocalSize(vht->gvelocity,&nu);dCHK(err);
    err = VecGetLocalSize(vht->gpressure,&np);dCHK(err);
    err = VecGetLocalSize(vht->genthalpy,&ne);dCHK(err);

    {                           /* Get local sizes of the closure */
      Vec  Vc,Vgh,Pc,Pgh,Ec,Egh;
      err = VecDohpGetClosure(vht->gvelocity,&Vc);dCHK(err);
      err = VecDohpGetClosure(vht->gpressure,&Pc);dCHK(err);
      err = VecDohpGetClosure(vht->genthalpy,&Ec);dCHK(err);
      err = VecGhostGetLocalForm(Vc,&Vgh);dCHK(err);
      err = VecGhostGetLocalForm(Pc,&Pgh);dCHK(err);
      err = VecGhostGetLocalForm(Ec,&Egh);dCHK(err);
      err = VecGetLocalSize(Vgh,&nul);dCHK(err);
      err = VecGetLocalSize(Pgh,&npl);dCHK(err);
      err = VecGetLocalSize(Egh,&nel);dCHK(err);
      err = VecGhostRestoreLocalForm(Vc,&Vgh);dCHK(err);
      err = VecGhostRestoreLocalForm(Pc,&Pgh);dCHK(err);
      err = VecGhostRestoreLocalForm(Ec,&Egh);dCHK(err);
      err = VecDohpRestoreClosure(vht->gvelocity,&Vc);dCHK(err);
      err = VecDohpRestoreClosure(vht->gpressure,&Pc);dCHK(err);
      err = VecDohpRestoreClosure(vht->genthalpy,&Ec);dCHK(err);
    }

    {                           /* Set up the Stokes sub-problem */
      IS   ublock,pblock;
      dInt rstart;
      err = VecCreateMPI(vht->comm,nu+np,PETSC_DETERMINE,&vht->stokes.x);dCHK(err);
      err = VecDuplicate(vht->stokes.x,&vht->stokes.y);dCHK(err);
      err = VecGetOwnershipRange(vht->stokes.x,&rstart,NULL);dCHK(err);
      err = ISCreateStride(vht->comm,nu,rstart,1,&ublock);dCHK(err);
      err = ISCreateStride(vht->comm,np,rstart+nu,1,&pblock);dCHK(err);
      err = ISSetBlockSize(ublock,3);dCHK(err);
      err = VecScatterCreate(vht->stokes.x,ublock,vht->gvelocity,NULL,&vht->stokes.extractVelocity);dCHK(err);
      err = VecScatterCreate(vht->stokes.x,pblock,vht->gpressure,NULL,&vht->stokes.extractPressure);dCHK(err);
      vht->stokes.ublock = ublock;
      vht->stokes.pblock = pblock;
      /* Create local index sets */
      err = ISCreateStride(PETSC_COMM_SELF,nul,0,1,&vht->stokes.lublock);dCHK(err);
      err = ISCreateStride(PETSC_COMM_SELF,npl,nul,1,&vht->stokes.lpblock);dCHK(err);
      err = ISSetBlockSize(vht->stokes.lublock,3);dCHK(err);
    }
    {                           /* Set up the Stokes sub-problem */
      IS   ublock,pblock,eblock;
      dInt rstart;
      err = VecCreateMPI(vht->comm,nu+np+ne,PETSC_DETERMINE,&vht->gpacked);dCHK(err);
      err = VecGetOwnershipRange(vht->gpacked,&rstart,NULL);dCHK(err);
      err = ISCreateStride(vht->comm,nu,rstart,1,&ublock);dCHK(err);
      err = ISCreateStride(vht->comm,np,rstart+nu,1,&pblock);dCHK(err);
      err = ISCreateStride(vht->comm,ne,rstart+nu+np,1,&eblock);dCHK(err);
      err = ISSetBlockSize(ublock,3);dCHK(err);
      err = VecScatterCreate(vht->gpacked,ublock,vht->gvelocity,NULL,&vht->all.extractVelocity);dCHK(err);
      err = VecScatterCreate(vht->gpacked,pblock,vht->gpressure,NULL,&vht->all.extractPressure);dCHK(err);
      err = VecScatterCreate(vht->gpacked,eblock,vht->genthalpy,NULL,&vht->all.extractEnthalpy);dCHK(err);
      vht->all.ublock = ublock;
      vht->all.pblock = pblock;
      vht->all.eblock = eblock;
      /* Create local index sets */
      err = ISCreateStride(PETSC_COMM_SELF,nul,0,1,&vht->all.lublock);dCHK(err);
      err = ISCreateStride(PETSC_COMM_SELF,npl,nul,1,&vht->all.lpblock);dCHK(err);
      err = ISCreateStride(PETSC_COMM_SELF,nel,nul+npl,1,&vht->all.leblock);dCHK(err);
      err = ISSetBlockSize(vht->all.lublock,3);dCHK(err);
    }
  }
  err = dJacobiDestroy(&jac);dCHK(err);
  err = dMeshDestroy(&mesh);dCHK(err);

  err = VHTCaseSetType(vht->scase,scasename);dCHK(err);
  err = dFSGetBoundingBox(vht->fsu,vht->scase->bbox);dCHK(err);
  err = VHTCaseSetFromOptions(vht->scase);dCHK(err);
  dFunctionReturn(0);
}

static dErr VHTGetRegionIterator(VHT vht,VHTEvaluation eval,dRulesetIterator *riter)
{
  dErr err;

  dFunctionBegin;
  if (!vht->regioniter[eval]) {
    dRulesetIterator iter;
    dRuleset ruleset;
    dFS cfs;
    dMeshESH domain;
    dQuadratureMethod qmethod;
    switch (eval) {
    case EVAL_FUNCTION: qmethod = vht->function_qmethod; break;
    case EVAL_JACOBIAN: qmethod = vht->jacobian_qmethod; break;
    default: dERROR(vht->comm,PETSC_ERR_ARG_OUTOFRANGE,"Unknown evaluation context");
    }
    err = dFSGetDomain(vht->fsu,&domain);dCHK(err);
    err = dFSGetPreferredQuadratureRuleSet(vht->fsu,domain,dTYPE_REGION,dTOPO_ALL,qmethod,&ruleset);dCHK(err);
    err = dFSGetCoordinateFS(vht->fsu,&cfs);dCHK(err);
    err = dRulesetCreateIterator(ruleset,cfs,&iter);dCHK(err);
    err = dRulesetDestroy(&ruleset);dCHK(err); /* Give ownership to iterator */
    err = dRulesetIteratorAddFS(iter,vht->fsu);dCHK(err);
    err = dRulesetIteratorAddFS(iter,vht->fsp);dCHK(err);
    err = dRulesetIteratorAddFS(iter,vht->fse);dCHK(err);
    if (eval == EVAL_FUNCTION) {err = dRulesetIteratorAddStash(iter,0,sizeof(struct VHTStash));dCHK(err);}
    vht->regioniter[eval] = iter;
  }
  *riter = vht->regioniter[eval];
  dFunctionReturn(0);
}

static dErr VHTExtractGlobalSplit(VHT vht,Vec X,Vec *Xu,Vec *Xp,Vec *Xe)
{
  dErr err;

  dFunctionBegin;
  if (Xu) {
    *Xu = vht->gvelocity;
    err = VecScatterBegin(vht->all.extractVelocity,X,*Xu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
    err = VecScatterEnd  (vht->all.extractVelocity,X,*Xu,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  }
  if (Xp) {
    *Xp = vht->gpressure;
    err = VecScatterBegin(vht->all.extractPressure,X,*Xp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
    err = VecScatterEnd  (vht->all.extractPressure,X,*Xp,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  }
  if (Xe) {
    *Xe = vht->genthalpy;
    err = VecScatterBegin(vht->all.extractEnthalpy,X,*Xe,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
    err = VecScatterEnd  (vht->all.extractEnthalpy,X,*Xe,INSERT_VALUES,SCATTER_FORWARD);dCHK(err);
  }
  dFunctionReturn(0);
}

static dErr VHTCommitGlobalSplit(VHT vht,Vec *gxu,Vec *gxp,Vec *gxe,Vec gy,InsertMode imode)
{
  dErr err;

  dFunctionBegin;
  dASSERT(*gxu == vht->gvelocity);
  dASSERT(*gxp == vht->gpressure);
  dASSERT(*gxe == vht->genthalpy);
  err = VecScatterBegin(vht->all.extractVelocity,*gxu,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractVelocity,*gxu,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterBegin(vht->all.extractPressure,*gxp,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractPressure,*gxp,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterBegin(vht->all.extractEnthalpy,*gxe,gy,imode,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractEnthalpy,*gxe,gy,imode,SCATTER_REVERSE);dCHK(err);
  *gxu = NULL;
  *gxp = NULL;
  dFunctionReturn(0);
}

static dErr VHTDestroy(VHT *invht)
{
  VHT vht = *invht;
  dErr err;

  dFunctionBegin;
  err = dFSDestroy(&vht->fsu);dCHK(err);
  err = dFSDestroy(&vht->fsp);dCHK(err);
  err = dFSDestroy(&vht->fse);dCHK(err);
  err = VecDestroy(&vht->xu);dCHK(err);
  err = VecDestroy(&vht->yu);dCHK(err);
  err = VecDestroy(&vht->xp);dCHK(err);
  err = VecDestroy(&vht->yp);dCHK(err);
  err = VecDestroy(&vht->xe);dCHK(err);
  err = VecDestroy(&vht->ye);dCHK(err);
  err = VecDestroy(&vht->gvelocity);dCHK(err);
  err = VecDestroy(&vht->gpressure);dCHK(err);
  err = VecDestroy(&vht->genthalpy);dCHK(err);
  err = VecDestroy(&vht->gpacked);dCHK(err);
  {
    err = ISDestroy(&vht->stokes.ublock);dCHK(err);
    err = ISDestroy(&vht->stokes.pblock);dCHK(err);
    err = ISDestroy(&vht->stokes.lublock);dCHK(err);
    err = ISDestroy(&vht->stokes.lpblock);dCHK(err);
    err = VecScatterDestroy(&vht->stokes.extractVelocity);dCHK(err);
    err = VecScatterDestroy(&vht->stokes.extractPressure);dCHK(err);
    err = VecDestroy(&vht->stokes.x);dCHK(err);
    err = VecDestroy(&vht->stokes.y);dCHK(err);
  }
  {
    err = ISDestroy(&vht->all.ublock);dCHK(err);
    err = ISDestroy(&vht->all.pblock);dCHK(err);
    err = ISDestroy(&vht->all.eblock);dCHK(err);
    err = ISDestroy(&vht->all.lublock);dCHK(err);
    err = ISDestroy(&vht->all.lpblock);dCHK(err);
    err = ISDestroy(&vht->all.leblock);dCHK(err);
    err = VecScatterDestroy(&vht->all.extractVelocity);dCHK(err);
    err = VecScatterDestroy(&vht->all.extractPressure);dCHK(err);
    err = VecScatterDestroy(&vht->all.extractEnthalpy);dCHK(err);
    err = VecScatterDestroy(&vht->all.extractStokes);dCHK(err);
  }
  err = dFree(vht->log.epochs);dCHK(err);
  for (dInt i=0; i<EVAL_UB; i++) {err = dRulesetIteratorDestroy(&vht->regioniter[i]);dCHK(err);}
  err = VHTCaseDestroy(&vht->scase);dCHK(err);
  err = dFree(*invht);dCHK(err);
  dFunctionReturn(0);
}


static dErr VHTGetMatrices(VHT vht,dBool use_jblock,Mat *J,Mat *P)
{
  dErr err;
  dInt m,nu,np,ne;
  Mat Juu,Jup,Jue,Jpu,Jpp,Jpe,Jeu,Jep,Jee,Buu,Bpp,Bee;
  IS splitis[3];

  dFunctionBegin;
  err = VecGetLocalSize(vht->gpacked,&m);dCHK(err);
  err = VecGetLocalSize(vht->gvelocity,&nu);dCHK(err);
  err = VecGetLocalSize(vht->gpressure,&np);dCHK(err);
  err = VecGetLocalSize(vht->genthalpy,&ne);dCHK(err);

  /* Create high-order matrix for diagonal velocity block, with context \a vht */
  err = MatCreateShell(vht->comm,nu,nu,PETSC_DETERMINE,PETSC_DETERMINE,vht,&Juu);dCHK(err);
  err = MatShellSetOperation(Juu,MATOP_GET_VECS,(void(*)(void))MatGetVecs_VHT_stokes);dCHK(err);
  err = MatShellSetOperation(Juu,MATOP_MULT,(void(*)(void))MatMult_VHT_uu);dCHK(err);
  err = MatShellSetOperation(Juu,MATOP_MULT_TRANSPOSE,(void(*)(void))MatMult_VHT_uu);dCHK(err);
  err = MatShellSetOperation(Juu,MATOP_MULT_ADD,(void(*)(void))MatMultAdd_VHT_uu);dCHK(err);
  err = MatShellSetOperation(Juu,MATOP_MULT_TRANSPOSE_ADD,(void(*)(void))MatMultAdd_VHT_uu);dCHK(err);
  err = MatSetOptionsPrefix(Juu,"Juu_");dCHK(err);

  /* Create off-diagonal high-order matrix, with context \a vht */
  err = MatCreateShell(vht->comm,np,nu,PETSC_DETERMINE,PETSC_DETERMINE,vht,&Jpu);dCHK(err);
  err = MatShellSetOperation(Jpu,MATOP_GET_VECS,(void(*)(void))MatGetVecs_VHT_stokes);dCHK(err);
  err = MatShellSetOperation(Jpu,MATOP_MULT,(void(*)(void))MatMult_VHT_pu);dCHK(err);
  err = MatShellSetOperation(Jpu,MATOP_MULT_TRANSPOSE,(void(*)(void))MatMult_VHT_up);dCHK(err);
  err = MatShellSetOperation(Jpu,MATOP_MULT_ADD,(void(*)(void))MatMultAdd_VHT_pu);dCHK(err);
  err = MatShellSetOperation(Jpu,MATOP_MULT_TRANSPOSE_ADD,(void(*)(void))MatMultAdd_VHT_up);dCHK(err);
  err = MatCreateTranspose(Jpu,&Jup);dCHK(err);
  err = MatSetOptionsPrefix(Jpu,"Jpu_");dCHK(err);
  err = MatSetOptionsPrefix(Jup,"Jup_");dCHK(err);

  /* These entries are really zero */
  Jpp = NULL;
  Jpe = NULL;
  Jep = NULL;

  /* @todo These off-diagonal blocks are not actually zero. Assume coupled application of the Jacobian and additive fieldsplit at this point */
  Jue = NULL;
  Jeu = NULL;

  /* Enthalpy-enthalpy coupling */
  err = MatCreateShell(vht->comm,ne,ne,PETSC_DETERMINE,PETSC_DETERMINE,vht,&Jee);dCHK(err);
  err = MatShellSetOperation(Jee,MATOP_GET_VECS,(void(*)(void))MatGetVecs_VHT_ee);dCHK(err);
  err = MatShellSetOperation(Jee,MATOP_MULT,(void(*)(void))MatMult_VHT_ee);dCHK(err);
  err = MatSetOptionsPrefix(Jee,"Jee_");dCHK(err);

  splitis[0] = vht->all.ublock;
  splitis[1] = vht->all.pblock;
  splitis[2] = vht->all.eblock;
  /* Create the matrix-free operator */
  err = MatCreateNest(vht->comm,3,splitis,3,splitis,((Mat[]){Juu,Jup,Jue, Jpu,Jpp,Jpe, Jeu,Jep,Jee}),J);dCHK(err);
  err = MatSetOptionsPrefix(*J,"J_");dCHK(err);
  err = MatSetFromOptions(*J);dCHK(err);
  if (!use_jblock) {
    err = MatShellSetOperation(*J,MATOP_MULT,(void(*)(void))MatMult_Nest_VHT_all);dCHK(err);
  }

  err = MatDestroy(&Juu);dCHK(err);
  err = MatDestroy(&Jup);dCHK(err);
  err = MatDestroy(&Jue);dCHK(err);
  err = MatDestroy(&Jpu);dCHK(err);
  err = MatDestroy(&Jpp);dCHK(err);
  err = MatDestroy(&Jpe);dCHK(err);
  err = MatDestroy(&Jeu);dCHK(err);
  err = MatDestroy(&Jep);dCHK(err);
  err = MatDestroy(&Jee);dCHK(err);

  /* Create real matrix to be used for preconditioning */
  err = dFSGetMatrix(vht->fsu,vht->mattype_Buu,&Buu);dCHK(err);
  err = dFSGetMatrix(vht->fsp,vht->mattype_Bpp,&Bpp);dCHK(err);
  err = dFSGetMatrix(vht->fse,vht->mattype_Bee,&Bee);dCHK(err);
  err = MatSetOptionsPrefix(Buu,"Buu_");dCHK(err);
  err = MatSetOptionsPrefix(Bpp,"Bpp_");dCHK(err);
  err = MatSetOptionsPrefix(Bee,"Bee_");dCHK(err);
  err = MatSetOption(Buu,MAT_SYMMETRIC,PETSC_TRUE);dCHK(err);
  err = MatSetOption(Bpp,MAT_SYMMETRIC,PETSC_TRUE);dCHK(err);
  err = MatSetFromOptions(Buu);dCHK(err);
  err = MatSetFromOptions(Bpp);dCHK(err);
  err = MatSetFromOptions(Bee);dCHK(err);
  err = MatCreateNest(vht->comm,3,splitis,3,splitis,((Mat[]){Buu,NULL,NULL, NULL,Bpp,NULL, NULL,NULL,Bee}),P);dCHK(err);
  err = MatSetOptionsPrefix(*P,"B_");dCHK(err);
  err = MatSetFromOptions(*P);dCHK(err);

  err = MatDestroy(&Buu);dCHK(err);
  err = MatDestroy(&Bpp);dCHK(err);
  err = MatDestroy(&Bee);dCHK(err);
  dFunctionReturn(0);
}

// The "physics" functions below perform forward-mode derivative propagation.
// Every argument depending on model state U is accompanied by a dual component U1.
static inline void VHTRheoSplice(dScalar a,dScalar a1,dScalar a1x,dScalar b,dScalar b1,dScalar b1x,dReal x0,dReal x01,dReal width,dScalar x,dScalar x1,dScalar *y,dScalar *y1,dScalar *y1x,dScalar *y1x1)
{ // Smooth transition from state a to state b at x0 over width
  // Propagates two derivatives:
  //   a1,b1,x01,x1 is a standard perturbation
  //   a1x,b1x are derivatives with respect to x
  dScalar
    arg = (x-x0)/width,
    arg_x = 1/width,
    f   = 1 + tanh(arg),
    f_x = (1 - dSqr(tanh(arg))) * arg_x,
    f_xx = -2 * tanh(arg) * f_x * arg_x * arg_x;
  *y = a + (b-a)/2 * f;
  *y1 = a1 + (b1-a1)/2 * f + (b - a)/2 * f_x * (x1 - x01);
  *y1x = a1x + (b1x-a1x)/2 * f + (b - a)/2 * f_x;
  // For the derivative of y1x with moment, we simplify since currently a1x, b1x are independent of x
  *y1x1 = ((b1x-a1x)/2 * f_x * (x1-x01)
           + (b1-a1)/2 * f_x + (b-a)/2 * f_xx * (x1 - x01));
}
static dErr VHTRheoSolveEqStateTangent(struct VHTRheology *rheo,const dScalar rhou[3],const dScalar rhou1[3],dScalar p,dScalar p1,dScalar E,dScalar E1,
                                       const dScalar drhou[9],const dScalar drhou1[9],const dScalar dE[3],const dScalar dE1[3],
                                       dScalar *T,dScalar *T1,dScalar *omega,dScalar *omega1,dScalar *rho,dScalar *rho1,
                                       dScalar dT[3],dScalar dT1[3],dScalar domega[3],dScalar domega1[3])
{
  const dScalar
    rhotmp = rheo->rhoi, // cheat
    Tm = rheo->T3 - rheo->beta_CC * p,
    Tm1 = -rheo->beta_CC * p1,
    em = rheo->c_i * (Tm - rheo->T0),
    em1 = rheo->c_i * Tm1;
  dScalar e,e1,de[3],de1[3],T1e,T1e1,omega1e,omega1e1;

  dFunctionBegin;
  e = (E - 1/(2*rhotmp) * dDotScalar3(rhou,rhou)) / rhotmp;
  e1 = (E1 - 1/(rhotmp) * dDotScalar3(rhou1,rhou)) / rhotmp;
  for (dInt i=0; i<3; i++) {
    de[i] = (dE[i] - 1/rhotmp * dDotScalarColumn3(rhou,drhou,i)) / rhotmp;
    de1[i] = (dE1[i] - 1/rhotmp * (dDotScalarColumn3(rhou1,drhou,i) + dDotScalarColumn3(rhou,drhou1,i))) / rhotmp;
  }
  VHTRheoSplice(rheo->T0+e/rheo->c_i,e1/rheo->c_i,1/rheo->c_i, Tm,Tm1,0, em,em1,rheo->splice_delta, e,e1, T,T1,&T1e,&T1e1);
  for (dInt i=0; i<3; i++) {
    dT[i] = T1e * de[i];
    dT1[i] = T1e1 * de[i] + T1e * de1[i];
  }
  VHTRheoSplice(0,0,0, (e-em)/rheo->Latent,e1/rheo->Latent,1/rheo->Latent, em,em1,rheo->splice_delta, e,e1, omega,omega1,&omega1e,&omega1e1);
  for (dInt i=0; i<3; i++) {
    domega[i] = omega1e * de[i];
    domega1[i] = omega1e1 * de[i] + omega1e * de1[i];
  }
  *rho = (1-omega[0]) * rheo->rhoi + omega[0]*rheo->rhow;
  *rho1 = (rheo->rhow - rheo->rhoi) * omega1[0];
  dFunctionReturn(0);
}
static dErr VHTRheoSolveEqState(struct VHTRheology *rheo,const dScalar rhou[3],dScalar p,dScalar E, const dScalar drhou[9],const dScalar dE[3],
                                       dScalar *T,dScalar *T1E,dScalar *omega,dScalar *omega1E,dScalar *rho,dScalar *rho1E,
                                       dScalar dT[3],dScalar domega[3])
{ // This version is slightly less verbose. It only provides derivatives with respect to total energy.  It is a
  // reasonable approximation that dT points in the same direction as dE because the processes that can change that
  // (large kinetic energy or pressure-dependence of temperature) are either not significant in glaciology or act on
  // much slower time scales.
  const dScalar rhou1[3] = {0,0,0},p1 = 0,E1 = 1,drhou1[9] = {0,0,0,0,0,0,0,0,0},dE1[3] = {0,0,0};
  dScalar dT1[3],domega1[3];

  dErr err;
  dFunctionBegin;
  err = VHTRheoSolveEqStateTangent(rheo,rhou,rhou1,p,p1,E,E1, drhou,drhou1,dE,dE1, T,T1E,omega,omega1E,rho,rho1E, dT,dT1,domega,domega1);dCHK(err);
  dFunctionReturn(0);
}
static dErr VHTRheoArrhenius(struct VHTRheology *rheo,dScalar p,dScalar p1,dScalar T,dScalar T1,dScalar omega,dScalar omega1,dScalar *B,dScalar *B1)
{
  dScalar
    n          = 1./(rheo->pe-1),
    Tstar      = T - rheo->beta_CC*p,
    Tstar1     = T1 - rheo->beta_CC*p1,
    expargnum  = rheo->Q*(rheo->T0 - Tstar) - p*rheo->V,
    expargnum1 = -rheo->Q * Tstar1 - p1*rheo->V,
    expargden  = n * rheo->R * rheo->T0 * Tstar,
    expargden1 = n * rheo->R * rheo->T0 * Tstar1,
    exparg     =  expargnum / expargden,
    exparg1    = expargnum1 / expargden - expargnum / dSqr(expargden) * expargden1,
    warg       = 1 + rheo->Bomega * omega,
    warg1      = rheo->Bomega * omega1,
    wpow       = pow(warg, -1/n),
    wpow1      = -1/n * wpow / warg * warg1;
  dFunctionBegin;
  dASSERT(-10 < exparg && exparg < 10);
  *B  = rheo->B0 * exp(exparg) * wpow;
  *B1 = rheo->B0 * exp(exparg) * (exparg1*wpow + wpow1);
  dFunctionReturn(0);
}
static dErr VHTRheoViscosity(struct VHTRheology *rheo,dScalar p,dScalar T,dScalar T1,dScalar omega,dScalar omega1,const dScalar Du[6],dScalar *eta,dScalar *eta1gamma,dScalar *eta1E)
{
  const dScalar
    pe = rheo->pe,
    gamma_reg = dSqr(rheo->eps) + 0.5*dColonSymScalar3(Du,Du)/rheo->gamma0,
    power = pow(gamma_reg,0.5*(pe-2)),
    power1gamma = 0.5*(pe-2) * power / gamma_reg;
  dScalar B,B1E;
  dErr err;

  dFunctionBegin;
  err = VHTRheoArrhenius(rheo,p,0,T,T1,omega,omega1,&B,&B1E);dCHK(err);
  dASSERT(dSqr(rheo->eps) <= gamma_reg && gamma_reg < 1e4);
  *eta = B * power;
  *eta1gamma = B * power1gamma / rheo->gamma0;
  *eta1E = B1E * power;
  dFunctionReturn(0);
}

static void VHTPointwiseGetDui(const struct VHTStash *st,const dScalar drhou[9],dScalar Dui[6])
{
  dScalar du[9];
  for (dInt i=0; i<9; i++) du[i] = drhou[i] / st->rho;
  dTensorSymCompress3(du,Dui);
}
static dErr VHTPointwiseComputeStash(struct VHTRheology *rheo,const dScalar rhou[3],const dScalar drhou[9],const dScalar p[1],const dScalar dUNUSED dp[3],const dScalar E[1],const dScalar dE[3],struct VHTStash *st)
{
  dErr err;
  dScalar T,omega,domega[3],rho1E;

  dFunctionBegin;
  memset(st,0xff,sizeof(*st));
  dMakeMemUndefined(st,sizeof(*st));
  err = VHTRheoSolveEqState(rheo,rhou,p[0],E[0], drhou,dE, &T,&st->T1E,&omega,&st->omega1E,&st->rho,&rho1E, st->dT,domega);dCHK(err);
  for (dInt i=0; i<3; i++) st->wmom[i] = -rheo->kappa_w * domega[i];
  for (dInt i=0; i<3; i++) st->u[i] = rhou[i] / st->rho;
  VHTPointwiseGetDui(st,drhou,st->Dui);
  st->E = E[0];
  err = VHTRheoViscosity(rheo,p[0],T,st->T1E,omega,st->omega1E,st->Dui,&st->eta,&st->eta1gamma,&st->eta1E);dCHK(err);
  //dRealTableView(sizeof(*st)/sizeof(dReal),1,(dReal*)st,PETSC_VIEWER_STDOUT_WORLD,"stash");
  dFunctionReturn(0);
}

static inline void VHTPointwiseFunction(VHTCase scase,const dReal x[3],dReal weight,
                                        const dScalar rhou[3],const dScalar drhou[9],const dScalar p[1],const dScalar dp[3],const dScalar E[1],const dScalar dE[3],
                                        struct VHTStash *st,
                                        dScalar rhou_[3],dScalar drhou_[6],dScalar p_[1],dScalar E_[1],dScalar dE_[3])
{
  struct VHTRheology *rheo = &scase->rheo;
  dScalar frhou[3],fp[1],fE[1],ui[3],heatflux[3],Sigma,symstress[6],stress[9];
  VHTPointwiseComputeStash(rheo,rhou,drhou,p,dp,E,dE,st);
  scase->forcing(scase,x,frhou,fp,fE);
  for (dInt i=0; i<3; i++) ui[i] = st->u[i] - st->wmom[i]/st->rho;
  for (dInt i=0; i<3; i++) heatflux[i] = -rheo->k_T*st->dT[i] + rheo->Latent*st->wmom[i];
  for (dInt i=0; i<6; i++) symstress[i] = st->eta * st->Dui[i] - (i<3)*p[0]; // eta Du - p I
  dTensorSymUncompress3(symstress,stress);
  Sigma = dColonSymScalar3(st->Dui,symstress);                                   // Strain heating
  for (dInt i=0; i<3; i++) rhou_[i] = -weight * frhou[i];                            // Momentum forcing term
  for (dInt i=0; i<3; i++) for (dInt j=0; j<3; j++) drhou_[i*3+j] = -weight * (rhou[i]*st->u[j] - stress[i*3+j]);
  p_[0] = -weight * (drhou[0]+drhou[4]+drhou[8] + fp[0]); // -q tr(drhou) - forcing, note tr(drhou) = div(rhou)
  E_[0] = -weight * (Sigma + fE[0]);                                           // Strain heating and thermal forcing
  for (dInt i=0; i<3; i++) dE_[i] = -weight * (ui[i]*E[0] + heatflux[i]);      // Transport and diffusion
}
static void VHTPointwiseJacobian(struct VHTRheology *rheo,const struct VHTStash *restrict st,dReal weight,
                                        const dScalar rhou[3],const dScalar drhou[9],const dScalar p[1],const dScalar E[1],const dScalar dE[3],
                                        dScalar rhou_[3],dScalar drhou_[9],dScalar p_[1],dScalar E_[1],dScalar dE_[3])
{ // This is not full Newton linearization. For that, I fear that we need AD.
  dScalar deta_colon,Dui[6],symstress[6],stress[9],Sigma1,ui[3];
  VHTPointwiseGetDui(st,drhou,Dui);
  deta_colon = st->eta1gamma * dColonSymScalar3(st->Dui,Dui);
  for (dInt i=0; i<6; i++) symstress[i] = st->eta * Dui[i] + deta_colon * st->Dui[i] + st->eta1E*E[0]*st->Dui[i] - p[0]*(i<3);
  dTensorSymUncompress3(symstress,stress);
  Sigma1 = 2*st->eta*dColonSymScalar3(st->Dui,Dui) + deta_colon*dColonSymScalar3(st->Dui,st->Dui) + st->eta1E*E[0]*dColonSymScalar3(st->Dui,st->Dui);

  for (dInt i=0; i<3; i++) rhou_[i] = 0;
  for (dInt i=0; i<3; i++) for (dInt j=0; j<3; j++) drhou_[i*3+j] = -weight * (rhou[i]*st->u[j] + st->u[i]*rhou[j] - stress[i*3+j]);
  p_[0] = -weight*(drhou[0]+drhou[4]+drhou[8]);                 // -q tr(Du)
  E_[0] = -weight*Sigma1;
  for (dInt i=0; i<3; i++) ui[i] = st->u[i] - st->wmom[i] / st->rho;
  for (dInt i=0; i<3; i++) dE_[i] = -weight * (ui[i] * E[0] + rhou[i]/st->rho * st->E
                                               - rheo->k_T * st->T1E * dE[i]
                                               - rheo->Latent * rheo->kappa_w * st->omega1E * dE[i]);
}

static void VHTPointwiseJacobian_uu(const struct VHTStash *st,dReal weight,const dScalar drhou[9],dScalar drhou_[9])
{
  dScalar Dui[6],deta_colon,stress[9],symstress[6];
  VHTPointwiseGetDui(st,drhou,Dui);
  deta_colon = st->eta1gamma*dColonSymScalar3(st->Dui,Dui);
  for (dInt i=0; i<6; i++) symstress[i] = st->eta*Dui[i] + deta_colon*st->Dui[i];
  dTensorSymUncompress3(symstress,stress);
  for (dInt i=0; i<9; i++) drhou_[i] = weight * drhou[i]; // @bug stress[i];
}

static void VHTPointwiseJacobian_pu(dReal weight,const dScalar drhou[9],dScalar p_[1])
{
  p_[0] = -weight*(drhou[0]+drhou[4]+drhou[8]);
}

static void VHTPointwiseJacobian_up(dReal weight,dScalar p,dScalar drhou_[6])
{
  drhou_[0] = -weight*p;
  drhou_[4] = -weight*p;
  drhou_[8] = -weight*p;
}

static void VHTPointwiseJacobian_ee(const struct VHTRheology *rheo,const struct VHTStash *st,dReal weight,const dScalar E[1],const dScalar dE[3],dScalar E_[1],dScalar dE_[3])
{
  dScalar ui[3];
  for (dInt i=0; i<3; i++) ui[i] = st->u[i] - st->wmom[i] / st->rho;
  E_[0] = -weight * st->eta1E * E[0] * dColonSymScalar3(st->Dui,st->Dui);
  for (dInt i=0; i<3; i++) dE_[i] = -weight * (ui[i] * E[0]
                                               - rheo->k_T * st->T1E * dE[i]
                                               - rheo->Latent * rheo->kappa_w * st->omega1E * dE[i]);
}

static dErr VHTFunction(SNES dUNUSED snes,Vec X,Vec Y,void *ctx)
{
  VHT              vht = ctx;
  dErr             err;
  Vec              Coords,Xu,Xp,Xe;
  dRulesetIterator iter;

  dFunctionBegin;
  err = VHTLogEpochStart(&vht->log);dCHK(err);
  err = VHTExtractGlobalSplit(vht,X,&Xu,&Xp,&Xe);dCHK(err);
  err = VHTGetRegionIterator(vht,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, Xu,dFS_INHOMOGENEOUS,Xu,dFS_INHOMOGENEOUS, Xp,dFS_INHOMOGENEOUS,Xp,dFS_INHOMOGENEOUS, Xe,dFS_INHOMOGENEOUS,Xe,dFS_INHOMOGENEOUS);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*u)[3],(*du)[9],(*p)[1],(*dp)[3],(*e)[1],(*de)[3];
    dScalar (*u_)[3],(*du_)[9],(*p_)[1],(*e_)[1],(*de_)[3];
    dInt Q;
    struct VHTStash *stash;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,&u_,&du_, &p,&dp,&p_,NULL, &e,&de,&e_,&de_);dCHK(err);
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      VHTPointwiseFunction(vht->scase,x[i],jw[i], u[i],du[i],p[i],dp[i],e[i],de[i], &stash[i], u_[i],du_[i],p_[i],e_[i],de_[i]);
      VHTLogStash(&vht->log,&vht->scase->rheo,dx[i],&stash[i]);
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, u_,du_, p_,NULL, e_,de_);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = VHTCommitGlobalSplit(vht,&Xu,&Xp,&Xe,Y,INSERT_VALUES);dCHK(err);
  err = VHTLogEpochEnd(&vht->log);dCHK(err);
  dFunctionReturn(0);
}

static dErr MatMult_Nest_VHT_all(Mat J,Vec X,Vec Y)
{
  VHT              vht;
  Vec              Coords,Xu,Xp,Xe;
  dRulesetIterator iter;
  dErr             err;
  Mat              A;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_VHTShellMult,J,X,Y,0);dCHK(err);
  err = MatNestGetSubMat(J,0,0,&A);dCHK(err);
  err = MatShellGetContext(A,(void**)&vht);dCHK(err);
  err = VHTExtractGlobalSplit(vht,X,&Xu,&Xp,&Xe);dCHK(err);
  err = VHTGetRegionIterator(vht,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, Xu,dFS_HOMOGENEOUS,Xu,dFS_HOMOGENEOUS, Xp,dFS_HOMOGENEOUS,Xp,dFS_HOMOGENEOUS, Xe,dFS_HOMOGENEOUS,Xe,dFS_HOMOGENEOUS);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*u)[3],(*du)[9],(*p)[1],(*e)[1],(*de)[3];
    dScalar (*u_)[3],(*du_)[9],(*p_)[1],(*e_)[1],(*de_)[3];
    dInt Q;
    struct VHTStash *stash;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,&u_,&du_, &p,NULL,&p_,NULL, &e,&de,&e_,&de_);dCHK(err);
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      VHTPointwiseJacobian(&vht->scase->rheo,&stash[i],jw[i],u[i],du[i],p[i],e[i],de[i],u_[i],du_[i],p_[i],e_[i],de_[i]);
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, u_,du_, p_,NULL, e_,de_);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = VHTCommitGlobalSplit(vht,&Xu,&Xp,&Xe,Y,INSERT_VALUES);dCHK(err);
  err = PetscLogEventEnd(LOG_VHTShellMult,J,X,Y,0);dCHK(err);
  dFunctionReturn(0);
}

static dErr MatMultXIorA_VHT_stokes(Mat A,Vec X,Vec Y,Vec Z,InsertMode imode,VHTMultMode mmode)
{
  VHT           vht;
  dRulesetIterator iter;
  Vec              Coords;
  dErr             err;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_VHTShellMult,A,X,Y,Z);dCHK(err);
  err = MatShellGetContext(A,(void**)&vht);dCHK(err);
  {  /* Check that we have correct sizes */
    dInt nu,np,nx,ny;
    err = VecGetSize(vht->gvelocity,&nu);dCHK(err);
    err = VecGetSize(vht->gpressure,&np);dCHK(err);
    err = VecGetSize(X,&nx);dCHK(err);
    err = VecGetSize(Y,&ny);dCHK(err);
    switch (mmode) {
    case VHT_MULT_UU: dASSERT(nx==nu && ny==nu); break;
    case VHT_MULT_UP: dASSERT(nx==np && ny==nu); break;
    case VHT_MULT_PU: dASSERT(nx==nu && ny==np); break;
    default: dERROR(PETSC_COMM_SELF,1,"Sizes do not match, unknown mult operation");
    }
  }

  switch (imode) {
  case INSERT_VALUES:
    if (Z) dERROR(vht->comm,PETSC_ERR_ARG_INCOMP,"Cannot use INSERT_VALUES and set gz");
    Z = Y;
    err = VecZeroEntries(Z);dCHK(err);
    break;
  case ADD_VALUES:
    if (Z != Y) {
      err = VecCopy(Y,Z);dCHK(err);
    }
    break;
  default: dERROR(vht->comm,PETSC_ERR_ARG_OUTOFRANGE,"unsupported imode");
  }

  err = VHTGetRegionIterator(vht,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  switch (mmode) {
  case VHT_MULT_UU:
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, X,dFS_HOMOGENEOUS,Z,dFS_HOMOGENEOUS, NULL,             NULL,              NULL,NULL);dCHK(err);
    break;
  case VHT_MULT_UP:
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, NULL,             Z,dFS_HOMOGENEOUS, X,dFS_HOMOGENEOUS,NULL,              NULL,NULL);dCHK(err);
    break;
  case VHT_MULT_PU:
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, X,dFS_HOMOGENEOUS,NULL,              NULL,             Z,dFS_HOMOGENEOUS, NULL,NULL);dCHK(err);
    break;
  default: dERROR(vht->comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid mmode");
  }
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*du)[9],(*du_)[9],*p,*p_;
    dInt Q;
    struct VHTStash *stash;
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    switch (mmode) {
    case VHT_MULT_UU:
      err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,&du,NULL,&du_, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);
      for (dInt i=0; i<Q; i++) {VHTPointwiseJacobian_uu(&stash[i],jw[i],du[i],du_[i]);}
      err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, NULL,du_, NULL,NULL, NULL,NULL);dCHK(err);
      break;
    case VHT_MULT_UP:
      err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,NULL,NULL,&du_, &p,NULL,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);
      for (dInt i=0; i<Q; i++) {VHTPointwiseJacobian_up(jw[i],p[i],du_[i]);}
      err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, NULL,du_, NULL,NULL, NULL,NULL);dCHK(err);
      break;
    case VHT_MULT_PU:
      err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,&du,NULL,NULL, NULL,NULL,&p_,NULL, NULL,NULL,NULL,NULL);dCHK(err);
      for (dInt i=0; i<Q; i++) {VHTPointwiseJacobian_pu(jw[i],du[i],&p_[i]);}
      err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, NULL,NULL, p_,NULL, NULL,NULL);dCHK(err);
      break;
    default: dERROR(vht->comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid mmode");
    }
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = PetscLogEventEnd(LOG_VHTShellMult,A,X,Y,Z);dCHK(err);
  dFunctionReturn(0);
}

static dErr MatMult_VHT_ee(Mat A,Vec X,Vec Y)
{
  VHT              vht;
  dRulesetIterator iter;
  Vec              Coords;
  dErr             err;

  dFunctionBegin;
  err = PetscLogEventBegin(LOG_VHTShellMult,A,X,Y,0);dCHK(err);
  err = MatShellGetContext(A,(void**)&vht);dCHK(err);
  err = VecZeroEntries(Y);dCHK(err);
  err = VHTGetRegionIterator(vht,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, NULL,NULL, NULL,NULL, X,Y);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dScalar *jw;
    dScalar (*x)[3],(*dx)[9],(*e)[1],(*de)[3],(*e_)[1],(*de_)[3];
    dInt Q;
    struct VHTStash *stash;
    err = dRulesetIteratorGetStash(iter,NULL,&stash);dCHK(err);
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL, &e,&de,&e_,&de_);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      VHTPointwiseJacobian_ee(&vht->scase->rheo,&stash[i],jw[i],e[i],de[i],e_[i],de_[i]);dCHK(err);
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, NULL,NULL, NULL,NULL, e_,de_);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = PetscLogEventEnd(LOG_VHTShellMult,A,X,Y,0);dCHK(err);
  dFunctionReturn(0);
}

static dErr VHTJacobianAssemble_Velocity(VHT vht,Mat Buu,Vec Mdiag,Vec X)
{
  dRulesetIterator iter;
  Vec Coords,Xu;
  dScalar *Kflat;
  dErr err;

  dFunctionBegin;
  err = VHTExtractGlobalSplit(vht,X,&Xu,NULL,NULL);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = VHTGetRegionIterator(vht,EVAL_JACOBIAN,&iter);dCHK(err);
  if (Mdiag) {
    err = VecZeroEntries(Mdiag);dCHK(err);
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, Xu,dFS_INHOMOGENEOUS,Mdiag,dFS_HOMOGENEOUS, NULL,NULL, NULL,NULL);dCHK(err);
  } else {
    err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, Xu,dFS_INHOMOGENEOUS,NULL, NULL,NULL, NULL,NULL);dCHK(err);
  }
  err = dRulesetIteratorGetMatrixSpaceSplit(iter, NULL,NULL,NULL,NULL, NULL,&Kflat,NULL,NULL, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw,*interp_flat,*deriv_flat;
    const dInt *rowcol;
    dScalar (*x)[3],(*dx)[3][3],(*u)[3],(*du)[9],(*v)[3],(*p)[1],(*dp)[3],(*e)[1],(*de)[3];
    dInt Q,P;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,&v,NULL, &p,&dp,NULL,NULL, &e,&de,NULL,NULL);dCHK(err);
    err = dRulesetIteratorGetPatchAssembly(iter, NULL,NULL,NULL,NULL, &P,&rowcol,&interp_flat,&deriv_flat, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);
    {                           /* Scope so that we can declare new VLA pointers for convenient assembly */
      const dReal (*interp)[P] = (const dReal(*)[P])interp_flat;
      const dReal (*deriv)[P][3] = (const dReal(*)[P][3])deriv_flat;
      dScalar (*K)[3][P][3] = (dScalar(*)[3][P][3])Kflat;
      err = PetscMemzero(K,P*3*P*3*sizeof(K[0][0][0][0]));dCHK(err);
      for (dInt q=0; q<Q; q++) {
        struct VHTStash stash;
        VHTPointwiseComputeStash(&vht->scase->rheo,u[q],du[q],p[q],dp[q],e[q],de[q],&stash);
        for (dInt j=0; j<P; j++) { /* trial functions */
          for (dInt fj=0; fj<3; fj++) {
            dScalar duu[3][3] = {{0},{0},{0}},du_[3][3];
            duu[fj][0] = deriv[q][j][0];
            duu[fj][1] = deriv[q][j][1];
            duu[fj][2] = deriv[q][j][2];
            VHTPointwiseJacobian_uu(&stash,jw[q],&duu[0][0],&du_[0][0]);
            for (dInt i=0; i<P; i++) {
              for (dInt fi=0; fi<3; fi++) {
                K[i][fi][j][fj] += (+ deriv[q][i][0] * du_[fi][0]
                                    + deriv[q][i][1] * du_[fi][1]
                                    + deriv[q][i][2] * du_[fi][2]);
              }
            }
          }
        }
      }
      err = dFSMatSetValuesBlockedExpanded(vht->fsu,Buu,8,rowcol,8,rowcol,&K[0][0][0][0],ADD_VALUES);dCHK(err);
      for (dInt i=0; i<P; i++) {
        dScalar Mentry = 0;
        for (dInt q=0; q<Q; q++) Mentry += interp[q][i] * jw[q] * interp[q][i]; /* Integrate the diagonal entry over this element */
        v[i][0] += Mentry;
        v[i][1] += Mentry;
        v[i][2] += Mentry;
      }
    }
    err = dRulesetIteratorCommitPatchApplied(iter,INSERT_VALUES, NULL,NULL, (dScalar**)&v,NULL, NULL,NULL, NULL,NULL);dCHK(err);
    err = dRulesetIteratorRestorePatchAssembly(iter, NULL,NULL,NULL,NULL, &P,&rowcol,&interp_flat,&deriv_flat, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  dFunctionReturn(0);
}

static dErr VHTJacobianAssemble_PressureEnthalpy(VHT vht,Mat Bpp,Mat Daux,Mat Bee,Vec X)
{
  dRulesetIterator iter;
  Vec              Coords,Xu,Xe;
  dScalar          *Kpp_flat,*Kppaux_flat,*Kee_flat;
  const dInt       *Ksizes;
  dErr             err;

  dFunctionBegin;
  /* It might seem weird to be getting velocity and enthalpy in the pressure assembly.  The reason is that this preconditioner
  * (indeed the entire problem) is always linear in pressure.  It \e might be nonlinear in velocity and enthalpy. */
  err = VHTExtractGlobalSplit(vht,X,&Xu,NULL,&Xe);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = VHTGetRegionIterator(vht,EVAL_JACOBIAN,&iter);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, Xu,dFS_INHOMOGENEOUS,NULL, NULL,NULL, Xe,dFS_INHOMOGENEOUS,NULL);dCHK(err);
  err = dRulesetIteratorGetMatrixSpaceSplit(iter, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL, NULL,NULL,&Kpp_flat,NULL, NULL,NULL,NULL,&Kee_flat);dCHK(err);
  err = dRulesetIteratorGetMatrixSpaceSizes(iter,NULL,NULL,&Ksizes);dCHK(err);
  err = dMallocA(Ksizes[2*4+2],&Kppaux_flat);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw,*interpp_flat,*derivp_flat,*interpe_flat,*derive_flat;
    const dInt *rowcolp,*rowcole;
    dScalar (*x)[3],(*dx)[3][3],(*u)[3],(*du)[9],(*p)[1],(*dp)[3],(*e)[1],(*de)[3];
    dInt Q,Pp,Pe;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,NULL,NULL, &p,&dp,NULL,NULL, &e,&de,NULL,NULL);dCHK(err);
    err = dRulesetIteratorGetPatchAssembly(iter, NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL, &Pp,&rowcolp,&interpp_flat,&derivp_flat, &Pe,&rowcole,&interpe_flat,&derive_flat);dCHK(err);
    {
      const dReal (*interpp)[Pp] = (const dReal(*)[Pp])interpp_flat;
      const dReal (*derivp)[Pp][3] = (const dReal(*)[Pp][3])derivp_flat;
      const dReal (*interpe)[Pe] = (const dReal(*)[Pe])interpe_flat;
      const dReal (*derive)[Pe][3] = (const dReal(*)[Pe][3])derive_flat;
      dScalar (*Kpp)[Pp] = (dScalar(*)[Pp])Kpp_flat,(*Kppaux)[Pp] = (dScalar(*)[Pp])Kppaux_flat;
      dScalar (*Kee)[Pe] = (dScalar(*)[Pe])Kee_flat;
      err = PetscMemzero(Kpp,Pp*Pp*sizeof(Kpp[0][0]));dCHK(err);
      err = PetscMemzero(Kppaux,Pp*Pp*sizeof(Kppaux[0][0]));dCHK(err);
      err = PetscMemzero(Kee,Pe*Pe*sizeof(Kee[0][0]));dCHK(err);
      for (dInt q=0; q<Q; q++) {
        struct VHTStash stash;
        err = VHTPointwiseComputeStash(&vht->scase->rheo,u[q],du[q],p[q],dp[q],e[q],de[q],&stash);dCHK(err);
        /* Pressure-pressure Jacobians  */
        for (dInt j=0; j<Pp; j++) { /* trial functions */
          for (dInt i=0; i<Pp; i++) {
            /* Scaled mass matrx */
            Kpp[i][j] += interpp[q][i] * jw[q] * (1./stash.eta) * interpp[q][j];
            /* Neumann Laplacian */
            Kppaux[i][j] += (+ derivp[q][i][0] * jw[q] * derivp[q][j][0]
                             + derivp[q][i][1] * jw[q] * derivp[q][j][1]
                             + derivp[q][i][2] * jw[q] * derivp[q][j][2]);
          }
        }
        /* Enthalpy-enthalpy Jacobian */
        for (dInt j=0; j<Pe; j++) {
          const dScalar ez[1] = {interpe[q][j]},dez[3] = {derive[q][j][0],derive[q][j][1],derive[q][j][2]};
          dScalar e_[1],de_[3];
          VHTPointwiseJacobian_ee(&vht->scase->rheo,&stash,jw[q],ez,dez,e_,de_);
          for (dInt i=0; i<Pe; i++) {
            Kee[i][j] += (interpe[q][i] * e_[0]
                          + derive[q][i][0] * de_[0]
                          + derive[q][i][1] * de_[1]
                          + derive[q][i][2] * de_[2]);
          }
        }
      }
      err = dFSMatSetValuesBlockedExpanded(vht->fsp,Bpp,Pp,rowcolp,Pp,rowcolp,&Kpp[0][0],ADD_VALUES);dCHK(err);
      if (Daux) {err = dFSMatSetValuesBlockedExpanded(vht->fsp,Daux,Pp,rowcolp,Pp,rowcolp,&Kppaux[0][0],ADD_VALUES);dCHK(err);}
      err = dFSMatSetValuesBlockedExpanded(vht->fse,Bee,Pe,rowcole,Pe,rowcole,&Kee[0][0],ADD_VALUES);dCHK(err);
    }
    err = dRulesetIteratorRestorePatchAssembly(iter, NULL,NULL,NULL,NULL, &Pp,&rowcolp,&interpp_flat,&derivp_flat, &Pe,&rowcole,&interpe_flat,&derive_flat);dCHK(err);
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = dFree(Kppaux_flat);dCHK(err);
  dFunctionReturn(0);
}


static dErr VHTJacobian(SNES dUNUSED snes,Vec X,Mat *J,Mat *B,MatStructure *structure,void *ctx)
{
  VHT vht = ctx;
  dErr err;
  Mat Buu,Bpp,Daux,Bee;
  Vec Mdiag;

  dFunctionBegin;
  err = MatGetLocalSubMatrix(*B,vht->all.lublock,vht->all.lublock,&Buu);dCHK(err);
  err = MatGetLocalSubMatrix(*B,vht->all.lpblock,vht->all.lpblock,&Bpp);dCHK(err);
  err = MatGetLocalSubMatrix(*B,vht->all.leblock,vht->all.leblock,&Bee);dCHK(err);
  err = PetscObjectQuery((dObject)Bpp,"LSC_M_diag",(dObject*)&Mdiag);dCHK(err);
  err = PetscObjectQuery((dObject)Bpp,"LSC_L",(dObject*)&Daux);dCHK(err);
  err = MatZeroEntries(*B);dCHK(err);
  if (Daux) {err = MatZeroEntries(Daux);dCHK(err);}
  err = VHTJacobianAssemble_Velocity(vht,Buu,Mdiag,X);dCHK(err);
  err = VHTJacobianAssemble_PressureEnthalpy(vht,Bpp,Daux,Bee,X);dCHK(err);
  if (Daux) {
    err = MatAssemblyBegin(Daux,MAT_FINAL_ASSEMBLY);dCHK(err);
    err = MatAssemblyEnd  (Daux,MAT_FINAL_ASSEMBLY);dCHK(err);
  }
  err = MatRestoreLocalSubMatrix(*B,vht->all.lublock,vht->all.lublock,&Buu);dCHK(err);
  err = MatRestoreLocalSubMatrix(*B,vht->all.lpblock,vht->all.lpblock,&Bpp);dCHK(err);
  err = MatRestoreLocalSubMatrix(*B,vht->all.leblock,vht->all.leblock,&Bee);dCHK(err);

  /* MatNest calls assembly on the constituent pieces */
  err = MatAssemblyBegin(*B,MAT_FINAL_ASSEMBLY);dCHK(err);
  err = MatAssemblyEnd(*B,MAT_FINAL_ASSEMBLY);dCHK(err);
  if (*J != *B) {
    err = MatAssemblyBegin(*J,MAT_FINAL_ASSEMBLY);dCHK(err);
    err = MatAssemblyEnd(*J,MAT_FINAL_ASSEMBLY);dCHK(err);
  }
  *structure = SAME_NONZERO_PATTERN;
  dFunctionReturn(0);
}

static dErr VHTGetPressureShift(VHT vht,Vec Xp,dScalar *pressureshift)
{
  dErr             err;
  dScalar          volume = 0,shift = 0;
  dRulesetIterator iter;
  Vec              Coords;

  dFunctionBegin;
  *pressureshift = 0;
  if (!vht->alldirichlet) dFunctionReturn(0);
   // Do a volume integral of the exact solution to that we can remove the constant pressure mode
  err = VHTGetRegionIterator(vht,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, NULL,NULL, Xp,dFS_INHOMOGENEOUS,NULL, NULL,NULL);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw;
    const dScalar (*x)[3],*p;
    dInt Q;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,NULL,NULL,NULL, NULL,NULL,NULL,NULL, &p,NULL,NULL,NULL, NULL,NULL,NULL,NULL);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      dScalar uu[3],duu[9],pp[1],dpp[3],ee[1],dee[3];
      err = vht->scase->solution(vht->scase,x[i],uu,duu,pp,dpp,ee,dee);dCHK(err);
      volume += jw[i];
      shift  += (pp[0] - p[i]) * jw[i]; // The computed pressure sum is zero, but the continuous integral may not be
    }
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  *pressureshift = shift/volume;
  dFunctionReturn(0);
}

static dErr VHTErrorNorms(VHT vht,Vec X,dReal N0u[3],dReal N1u[3],dReal N0p[3],dReal N1p[3],dReal N0e[3],dReal N1e[3])
{
  dErr             err;
  Vec              Coords,Xu,Xp,Xe;
  PetscScalar      pressureshift;
  dRulesetIterator iter;

  dFunctionBegin;
  err = dNormsStart(N0u,N1u);dCHK(err);
  err = dNormsStart(N0p,N1p);dCHK(err);
  err = dNormsStart(N0e,N1e);dCHK(err);
  err = VHTExtractGlobalSplit(vht,X,&Xu,&Xp,&Xe);dCHK(err);
  err = VHTGetRegionIterator(vht,EVAL_FUNCTION,&iter);dCHK(err);
  err = dFSGetGeometryVectorExpanded(vht->fsu,&Coords);dCHK(err);
  err = VHTGetPressureShift(vht,Xp,&pressureshift);dCHK(err);
  err = dRulesetIteratorStart(iter, Coords,dFS_INHOMOGENEOUS,NULL, Xu,dFS_INHOMOGENEOUS,NULL, Xp,dFS_INHOMOGENEOUS,NULL, Xe,dFS_INHOMOGENEOUS,NULL);dCHK(err);
  while (dRulesetIteratorHasPatch(iter)) {
    const dReal *jw;
    const dScalar (*x)[3],(*dx)[9],(*u)[3],(*du)[9],(*p)[1],(*dp)[3],(*e)[1],(*de)[3];
    dInt Q;
    err = dRulesetIteratorGetPatchApplied(iter,&Q,&jw, (dScalar**)&x,(dScalar**)&dx,NULL,NULL, &u,&du,NULL,NULL, &p,&dp,NULL,NULL, &e,&de,NULL,NULL);dCHK(err);
    for (dInt i=0; i<Q; i++) {
      dScalar uu[3],duu[9],pp[1],dpp[3],ee[1],dee[3];
      err = vht->scase->solution(vht->scase,x[i],uu,duu,pp,dpp,ee,dee);dCHK(err);
      pp[0] -= pressureshift;
      err = dNormsUpdate(N0u,N1u,jw[i],3,uu,u[i],duu,du[i]);dCHK(err);
      err = dNormsUpdate(N0p,N1p,jw[i],1,pp,p[i],dpp,dp[i]);dCHK(err);
      err = dNormsUpdate(N0e,N1e,jw[i],1,ee,e[i],dee,de[i]);dCHK(err);
    }
    err = dRulesetIteratorNextPatch(iter);dCHK(err);
  }
  err = dRulesetIteratorFinish(iter);dCHK(err);
  err = dNormsFinish(N0u,N1u);dCHK(err);
  err = dNormsFinish(N0p,N1p);dCHK(err);
  err = dNormsFinish(N0e,N1e);dCHK(err);
  dFunctionReturn(0);
}

// This function cannot runs separately for each field because the nodal basis may be different for each field
static dErr VHTGetSolutionField_All(VHT vht,dFS fs,dInt fieldnumber,Vec *insoln)
{
  Vec      Sol,Xc,Cvecg,Cvec;
  dScalar *x;
  const dScalar *coords;
  dInt     n,bs;
  dErr     err;

  dFunctionBegin;
  *insoln = 0;
  err = dFSCreateGlobalVector(fs,&Sol);dCHK(err);
  err = VecDohpGetClosure(Sol,&Xc);dCHK(err);
  err = dFSGetNodalCoordinatesGlobal(fs,&Cvecg);dCHK(err);
  err = VecDohpGetClosure(Cvecg,&Cvec);dCHK(err);
  err = VecGetLocalSize(Xc,&n);dCHK(err);
  err = VecGetBlockSize(Xc,&bs);dCHK(err);
  {
    dInt nc;
    err = VecGetLocalSize(Cvec,&nc);dCHK(err);
    if (nc*bs != n*3) dERROR(PETSC_COMM_SELF,1,"Coordinate vector has inconsistent size");
  }
  err = VecGetArray(Xc,&x);dCHK(err);
  err = VecGetArrayRead(Cvec,&coords);dCHK(err);
  for (dInt i=0; i<n/bs; i++) {
    dScalar u_unused[3],p_unused[1],du_unused[3*3],dp_unused[3],e_unused[1],de_unused[3];
    switch (fieldnumber) {
    case 0:
      err = vht->scase->solution(vht->scase,&coords[3*i],&x[i*bs],du_unused,p_unused,dp_unused,e_unused,de_unused);dCHK(err);
      break;
    case 1:
      err = vht->scase->solution(vht->scase,&coords[3*i],u_unused,du_unused,&x[i*bs],dp_unused,e_unused,de_unused);dCHK(err);
      break;
    case 2:
      err = vht->scase->solution(vht->scase,&coords[3*i],u_unused,du_unused,p_unused,dp_unused,&x[i*bs],de_unused);dCHK(err);
      break;
    default: dERROR(vht->comm,PETSC_ERR_ARG_OUTOFRANGE,"Requested field number %D",fieldnumber);
    }
  }
  err = VecRestoreArray(Xc,&x);dCHK(err);
  err = VecRestoreArrayRead(Cvec,&coords);dCHK(err);
  err = VecDohpRestoreClosure(Cvecg,&Cvec);dCHK(err);
  err = dFSInhomogeneousDirichletCommit(fs,Xc);dCHK(err);
  err = VecDohpRestoreClosure(Sol,&Xc);dCHK(err);
  *insoln = Sol;
  dFunctionReturn(0);
}

/** Creates a solution vector, commits the closure to each FS, returns packed solution vector */
static dErr VHTGetSolutionVector(VHT vht,Vec *insoln)
{
  dErr err;
  Vec Xu,Xp,Xe,spacked;

  dFunctionBegin;
  *insoln = 0;
  err = VHTGetSolutionField_All(vht,vht->fsu,0,&Xu);dCHK(err);
  err = VHTGetSolutionField_All(vht,vht->fsp,1,&Xp);dCHK(err);
  err = VHTGetSolutionField_All(vht,vht->fse,2,&Xe);dCHK(err);
  err = VecDuplicate(vht->gpacked,&spacked);dCHK(err);
  err = VecScatterBegin(vht->all.extractVelocity,Xu,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractVelocity,Xu,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterBegin(vht->all.extractPressure,Xp,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractPressure,Xp,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterBegin(vht->all.extractEnthalpy,Xe,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractEnthalpy,Xe,spacked,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecDestroy(&Xu);dCHK(err);
  err = VecDestroy(&Xp);dCHK(err);
  err = VecDestroy(&Xe);dCHK(err);
  *insoln = spacked;
  dFunctionReturn(0);
}

static dErr VHTGetNullSpace(VHT vht,MatNullSpace *matnull)
{
  dErr err;
  Vec r;

  dFunctionBegin;
  err = VecDuplicate(vht->gpacked,&r);dCHK(err);
  err = VecZeroEntries(r);dCHK(err);
  err = VecSet(vht->gpressure,1);dCHK(err);
  err = VecScatterBegin(vht->all.extractPressure,vht->gpressure,r,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecScatterEnd  (vht->all.extractPressure,vht->gpressure,r,INSERT_VALUES,SCATTER_REVERSE);dCHK(err);
  err = VecNormalize(r,PETSC_NULL);dCHK(err);
  err = MatNullSpaceCreate(vht->comm,dFALSE,1,&r,matnull);dCHK(err);
  err = VecDestroy(&r);dCHK(err);
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
  // At present, Jp intentionally contains an auxilliary matrix in the (p,p) block. It does not have the same null space as the Jacobian so we disable the error below.
  if (false && !isnull) dERROR(PETSC_COMM_SELF,1,"Vector is not in the null space of Jp");dCHK(err);
  err = MatDestroy(&mffd);dCHK(err);
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
    err = MatDestroy(&expmat);dCHK(err);
    err = MatDestroy(&expmat_fd);dCHK(err);
  }
  err = VecDestroy(&U);dCHK(err);
  err = VecDestroy(&F);dCHK(err);
  dFunctionReturn(0);
}


int main(int argc,char *argv[])
{
  VHT vht;
  MPI_Comm comm;
  Mat J,B;
  Vec R,X,Xsoln = NULL;
  SNES snes;
  dBool check_error,check_null,compute_explicit,use_jblock,viewdhm;
  dErr err;

  err = dInitialize(&argc,&argv,NULL,help);dCHK(err);
  comm = PETSC_COMM_WORLD;
  err = PetscLogEventRegister("VHTShellMult",MAT_CLASSID,&LOG_VHTShellMult);dCHK(err);

  err = VHTCaseRegisterAll();dCHK(err);
  err = VHTCreate(comm,&vht);dCHK(err);
  err = VHTSetFromOptions(vht);dCHK(err);

  err = VecDuplicate(vht->gpacked,&R);dCHK(err);
  err = VecDuplicate(R,&X);dCHK(err);

  err = PetscOptionsBegin(vht->comm,NULL,"VHT solver options",__FILE__);dCHK(err); {
    check_error = vht->scase->reality ? dFALSE : dTRUE;
    err = PetscOptionsBool("-check_error","Compute errors","",check_error,&check_error,NULL);dCHK(err);
    err = PetscOptionsBool("-use_jblock","Use blocks to apply Jacobian instead of unified (more efficient) version","",use_jblock=dFALSE,&use_jblock,NULL);dCHK(err);
    err = PetscOptionsBool("-viewdhm","View the solution","",viewdhm=dFALSE,&viewdhm,NULL);dCHK(err);
    err = PetscOptionsBool("-check_null","Check that constant pressure really is in the null space","",check_null=dFALSE,&check_null,NULL);dCHK(err);
    if (check_null) {
      err = PetscOptionsBool("-compute_explicit","Compute explicit Jacobian (only very small sizes)","",compute_explicit=dFALSE,&compute_explicit,NULL);dCHK(err);
    }
  } err = PetscOptionsEnd();dCHK(err);
  err = VHTGetMatrices(vht,use_jblock,&J,&B);dCHK(err);
  err = SNESCreate(comm,&snes);dCHK(err);
  err = SNESSetFunction(snes,R,VHTFunction,vht);dCHK(err);
  err = SNESSetJacobian(snes,J,B,VHTJacobian,vht);dCHK(err);
  err = SNESSetFromOptions(snes);dCHK(err);
  {
    KSP    ksp;
    PC     pc;

    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPGetPC(ksp,&pc);dCHK(err);
    err = PCFieldSplitSetIS(pc,"u",vht->all.ublock);dCHK(err);
    err = PCFieldSplitSetIS(pc,"p",vht->all.pblock);dCHK(err);
    err = PCFieldSplitSetIS(pc,"e",vht->all.eblock);dCHK(err);
  }
  err = VHTGetSolutionVector(vht,&Xsoln);dCHK(err);
  if (!vht->scase->reality) {
    dReal nrm;
    MatStructure mstruct;
    Vec b;
    err = VecDuplicate(X,&b);dCHK(err);
    err = VecZeroEntries(X);dCHK(err);
    err = SNESComputeFunction(snes,X,b);dCHK(err); /* -f */
    err = SNESComputeFunction(snes,Xsoln,R);dCHK(err);
    err = VHTLogReset(&vht->log);dCHK(err); // Exclude the evaluations above from the log
    err = VecNorm(R,NORM_2,&nrm);dCHK(err);
    err = dPrintf(comm,"Norm of discrete residual for exact solution %g\n",nrm);dCHK(err);
    err = SNESComputeJacobian(snes,Xsoln,&J,&B,&mstruct);dCHK(err);
    err = MatMult(J,Xsoln,R);dCHK(err);
    err = VecAXPY(R,1,b);dCHK(err); /* Jx - f */
    err = VecNorm(R,NORM_2,&nrm);dCHK(err);
    err = dPrintf(comm,"Norm of discrete linear residual at exact solution %g\n",nrm);dCHK(err);
    err = VecDestroy(&b);dCHK(err);
  }

  if (vht->alldirichlet) {                             /* Set null space */
    KSP ksp;
    MatNullSpace matnull;
    err = VHTGetNullSpace(vht,&matnull);dCHK(err);
    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPSetNullSpace(ksp,matnull);dCHK(err);
    if (Xsoln) {err = MatNullSpaceRemove(matnull,Xsoln,NULL);dCHK(err);}
    err = MatNullSpaceDestroy(&matnull);dCHK(err);
  }
  if (check_null) {
    err = CheckNullSpace(snes,R,compute_explicit);dCHK(err);
  }
  err = VecZeroEntries(R);dCHK(err);
  err = VecZeroEntries(X);dCHK(err);
  err = SNESSolve(snes,NULL,X);dCHK(err); /* ###  SOLVE  ### */
  err = VHTLogView(&vht->log,PETSC_VIEWER_STDOUT_WORLD);dCHK(err);
  if (vht->alldirichlet) {
    MatNullSpace matnull;
    KSP ksp;
    err = SNESGetKSP(snes,&ksp);dCHK(err);
    err = KSPGetNullSpace(ksp,&matnull);dCHK(err); /* does not reference */
    err = MatNullSpaceRemove(matnull,X,NULL);dCHK(err);
  }
  if (check_error) {
    dReal NAu[3],NIu[3],N0u[3],N1u[3],N0p[3],N1p[3],N0e[3],N1e[3];
    err = VHTErrorNorms(vht,X,N0u,N1u,N0p,N1p,N0e,N1e);dCHK(err);
    err = dNormsAlgebraicScaled(NAu,R);dCHK(err);
    err = VecWAXPY(R,-1,Xsoln,X);dCHK(err);
    err = dNormsAlgebraicScaled(NIu,R);dCHK(err);
    err = dPrintf(comm,"Algebraic residual        |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",NAu[0],NAu[1],NAu[2]);dCHK(err);
    err = dPrintf(comm,"Interpolation residual    |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",NIu[0],NIu[1],NIu[2]);dCHK(err);
    err = dPrintf(comm,"Integral velocity error 0 |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",N0u[0],N0u[1],N0u[2]);dCHK(err);
    err = dPrintf(comm,"Integral velocity error 1 |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",N1u[0],N1u[1],N1u[2]);dCHK(err);
    err = dPrintf(comm,"Integral pressure error 0 |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",N0p[0],N0p[1],N0p[2]);dCHK(err);
    err = dPrintf(comm,"Integral pressure error 1 |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",N1p[0],N1p[1],N1p[2]);dCHK(err);
    err = dPrintf(comm,"Integral enthalpy error 0 |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",N0e[0],N0e[1],N0e[2]);dCHK(err);
    err = dPrintf(comm,"Integral enthalpy error 1 |x|_1 %8.2e  |x|_2 %8.2e  |x|_inf %8.2e\n",N1e[0],N1e[1],N1e[2]);dCHK(err);
  }
  if (viewdhm) {
    Vec Xu,Xp,Xe;
    dViewer view;
    err = PetscViewerCreate(comm,&view);dCHK(err);
    err = PetscViewerSetType(view,PETSCVIEWERDHM);dCHK(err);
    err = PetscViewerFileSetName(view,"vht.dhm");dCHK(err);
    err = PetscViewerFileSetMode(view,FILE_MODE_WRITE);dCHK(err);
    err = VHTExtractGlobalSplit(vht,X,&Xu,&Xp,&Xe);dCHK(err);
    err = dFSDirichletProject(vht->fsu,Xu,dFS_INHOMOGENEOUS);dCHK(err);
    err = dFSDirichletProject(vht->fsp,Xp,dFS_INHOMOGENEOUS);dCHK(err);
    err = dFSDirichletProject(vht->fse,Xe,dFS_INHOMOGENEOUS);dCHK(err);
    err = VecView(Xu,view);dCHK(err);
    err = VecView(Xp,view);dCHK(err);
    err = VecView(Xe,view);dCHK(err);
    err = PetscViewerDestroy(&view);dCHK(err);
  }

  err = VecDestroy(&R);dCHK(err);
  err = VecDestroy(&X);dCHK(err);
  err = VecDestroy(&Xsoln);dCHK(err);
  err = SNESDestroy(&snes);dCHK(err);
  if (J != B) {err = MatDestroy(&J);dCHK(err);}
  err = MatDestroy(&B);dCHK(err);
  err = VHTDestroy(&vht);dCHK(err);
  err = dFinalize();dCHK(err);
  return 0;
}