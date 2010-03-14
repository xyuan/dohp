#include "modalimpl.h"
#include <dohp.h>
#include <dohpmesh.h>           /* iMesh_TypeFromTopology */

const char *const dJacobiModalFamilies[] = {
  "p-conforming",
  "p-discontinuous",
  "q-conforming",
  "q-discontinuous",
  "dJacobiModalFamilies",
  "dJACOBI_MODAL_",
  0
};

static dInt factorial(dInt n) {
  dInt i,f = 1;
  for (i=2; i<=n; i++) f *= i;
  return f;
}

static dInt choose(dInt n,dInt k) {
  return factorial(n) / (factorial(n-k)*factorial(k));
}

static dErr ModalPCount(dInt rdim,dInt order,dInt *count) {
  dFunctionBegin;
  switch (order) {
    case 0: *count = 1; break;
    case 1: *count = 1+rdim; break;
    case 2: *count = 1+rdim+rdim+choose(rdim,2); break;
    default: dERROR(PETSC_ERR_SUP,"Cannot do order %d\n",order);
  }
  dFunctionReturn(0);
}

/** Creates a ModalBasis corresponding to P_k elements **/
static dErr dJacobiModalBasisCreate(dJacobi jac,dEntTopology topo,dInt Q,const dReal rcoord[],dInt order,ModalBasis *basis)
{
  ModalBasis b;
  dInt P,rdim = iMesh_TypeFromTopology[topo];
  dErr err;

  dFunctionBegin;
  err = ModalPCount(rdim,order,&P);dCHK(err);
  err = dNewLog(jac,struct _ModalBasis,&b);dCHK(err);
  b->P   = P;
  b->Q   = Q;
  b->dim = rdim;
  err = dMallocA2(P*Q,&b->interp,rdim*P*Q,&b->deriv);dCHK(err);
  for (dInt i=0; i<Q; i++) {
    dReal *interp = &b->interp[i*P],(*deriv)[rdim] = (dReal(*)[rdim])&b->deriv[i*P*rdim];
    dReal x,y,z;
    x = rcoord[i*rdim+0];
    y = (rdim > 1) ? rcoord[i*rdim+1] : -1;
    z = (rdim > 2) ? rcoord[i*rdim+2] : -1;
    /* The following code was generated by sympy using Gram-Schmidt orthogonalization of the basis functions on the
    * reference element.  This produces a diagonal mass matrix. */
    switch (topo) {
      case dTOPO_LINE:
        switch (order) {
          case 2:
            interp[2] = sqrt(10.)/4. * (-1. + 3.*x*x);
            deriv[2][0] = sqrt(10.)/4. * 6.*x;
          case 1:
            interp[1] = x*sqrt(6)/2;
            deriv[1][0] = sqrt(6)/2;
          case 0:
            interp[0] = sqrt(2)/2;
            deriv[0][0] = 0;
        }
        break;
      case dTOPO_QUAD:
        switch (order) {
          case 2:
            interp[5] = 3*x*y/2;
            deriv[5][0] = 3*y/2;
            deriv[5][1] = 3*x/2;
            interp[4] = -sqrt(5)/4 + 3*sqrt(5)*pow(y,2)/4;
            deriv[4][0] = 0;
            deriv[4][1] = 3*y*sqrt(5)/2;
            interp[3] = -sqrt(5)/4 + 3*sqrt(5)*pow(x,2)/4;
            deriv[3][0] = 3*x*sqrt(5)/2;
            deriv[3][1] = 0;
          case 1:
            interp[2] = y*sqrt(3)/2;
            deriv[2][0] = 0;
            deriv[2][1] = sqrt(3)/2;
            interp[1] = x*sqrt(3)/2;
            deriv[1][0] = sqrt(3)/2;
            deriv[1][1] = 0;
          case 0:
            interp[0] = 1.0/2.0;
            deriv[0][0] = 0;
            deriv[0][1] = 0;
        }
        break;
      case dTOPO_HEX:
        switch (order) {
          case 2:
            interp[9] = 3*x*z*sqrt(2)/4;
            deriv[9][0] = 3*z*sqrt(2)/4;
            deriv[9][1] = 0;
            deriv[9][2] = 3*x*sqrt(2)/4;
            interp[8] = 3*y*z*sqrt(2)/4;
            deriv[8][0] = 0;
            deriv[8][1] = 3*z*sqrt(2)/4;
            deriv[8][2] = 3*y*sqrt(2)/4;
            interp[7] = 3*x*y*sqrt(2)/4;
            deriv[7][0] = 3*y*sqrt(2)/4;
            deriv[7][1] = 3*x*sqrt(2)/4;
            deriv[7][2] = 0;
            interp[6] = -sqrt(10)/8 + 3*sqrt(10)*pow(z,2)/8;
            deriv[6][0] = 0;
            deriv[6][1] = 0;
            deriv[6][2] = 3*z*sqrt(10)/4;
            interp[5] = -sqrt(10)/8 + 3*sqrt(10)*pow(y,2)/8;
            deriv[5][0] = 0;
            deriv[5][1] = 3*y*sqrt(10)/4;
            deriv[5][2] = 0;
            interp[4] = -sqrt(10)/8 + 3*sqrt(10)*pow(x,2)/8;
            deriv[4][0] = 3*x*sqrt(10)/4;
            deriv[4][1] = 0;
            deriv[4][2] = 0;
          case 1:
            interp[3] = z*sqrt(6)/4;
            deriv[3][0] = 0;
            deriv[3][1] = 0;
            deriv[3][2] = sqrt(6)/4;
            interp[2] = y*sqrt(6)/4;
            deriv[2][0] = 0;
            deriv[2][1] = sqrt(6)/4;
            deriv[2][2] = 0;
            interp[1] = x*sqrt(6)/4;
            deriv[1][0] = sqrt(6)/4;
            deriv[1][1] = 0;
            deriv[1][2] = 0;
          case 0:
            interp[0] = sqrt(2)/4;
            deriv[0][0] = 0;
            deriv[0][1] = 0;
            deriv[0][2] = 0;
        }
      default: dERROR(PETSC_ERR_SUP,"topology %s",iMesh_TopologyName[topo]);
    }
  }
  *basis = b;
  dFunctionReturn(0);
}

static dErr dJacobiModalGetBasis(dJacobi jac,dEntTopology topo,dInt Q,const dReal rcoord[],dInt order,ModalBasis *basis)
{
  dJacobi_Modal  *modal = jac->data;
  khash_t(modal) *hash = modal->topo[topo];
  ModalBasis     loc;
  dErr           err;
  int            key,new;
  khiter_t       k;

  dFunctionBegin;
  key = (topo<<24) | (Q<<8) | order;
  k = kh_put_modal(hash,key,&new);
  loc = kh_val(hash,key);
  if (new) {
    err = dJacobiModalBasisCreate(jac,topo,Q,rcoord,order,&loc);dCHK(err);
  }
  *basis = loc;
  dFunctionReturn(0);
}

dErr ModalBasisView(ModalBasis basis,PetscViewer viewer)
{
  dTruth ascii;
  dErr   err;

  dFunctionBegin;
  err = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&ascii);dCHK(err);
  if (!ascii) dFunctionReturn(0);
  err = PetscViewerASCIIPrintf(viewer,"TensorBasis with rule=%d basis=%d.\n",basis->Q,basis->P);dCHK(err);
  err = dRealTableView(basis->Q,basis->P,basis->interp,"interp",viewer);dCHK(err);
  err = dRealTableView(basis->Q,basis->P,basis->deriv,"deriv",viewer);dCHK(err);
  dFunctionReturn(0);
}

static dErr dJacobiDestroy_Modal(dJacobi jac)
{
  dJacobi_Modal *modal = jac->data;
  dErr err;

  dFunctionBegin;
  for (dInt i=0; i<dTOPO_ALL; i++) {
    khash_t(modal) *bases = modal->topo[i];
    if (!bases) continue;
    for (khiter_t k=kh_begin(bases); k!=kh_end(bases); k++) {
      ModalBasis b;
      if (!kh_exist(bases,k)) continue;
      b = kh_val(bases,k);
      err = dFree2(b->interp,b->deriv);dCHK(err);
      err = dFree(b);dCHK(err);
    }
    kh_destroy_modal(bases);
  }
  for (dQuadratureMethod m=0; m<dQUADRATURE_METHOD_INVALID; m++) {
    if (jac->quad[m]) {err = dQuadratureDestroy(jac->quad[m]);dCHK(err);}
  }
  err = dFree3(modal->efsOpsLine,modal->efsOpsQuad,modal->efsOpsHex);dCHK(err);
  err = dFree(modal);dCHK(err);
  dFunctionReturn(0);
}

static dErr dJacobiView_Modal(dJacobi jac,PetscViewer viewer)
{
  dJacobi_Modal *modal = jac->data;
  dTruth ascii;
  dErr err;

  dFunctionBegin;
  err = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&ascii);dCHK(err);
  if (!ascii) dERROR(PETSC_ERR_SUP,"only ASCII");
  err = PetscViewerASCIIPrintf(viewer,"Modal Jacobi\n");dCHK(err);
  err = PetscViewerASCIIPushTab(viewer);dCHK(err);
  for (dInt i=0; i<dTOPO_ALL; i++) {
    khash_t(modal) *bases = modal->topo[i];
    err = PetscViewerASCIIPrintf(viewer,"Database for %s\n",iMesh_TopologyName[i]);dCHK(err);
    err = PetscViewerASCIIPushTab(viewer);dCHK(err);
    for (khiter_t k=kh_begin(bases); k!=kh_end(bases); k++) {
      if (!kh_exist(bases,k)) continue;
      err = ModalBasisView(kh_val(bases,k),viewer);dCHK(err);
    }
    err = PetscViewerASCIIPopTab(viewer);dCHK(err);
  }
  err = PetscViewerASCIIPopTab(viewer);dCHK(err);
  dFunctionReturn(0);
}

static dErr dJacobiModalSetFamily_Modal(dJacobi jac,dJacobiModalFamily family)
{
  dJacobi_Modal *modal = jac->data;

  dFunctionBegin;
  if (family != dJACOBI_MODAL_P_DISCONTINUOUS)
    dERROR(PETSC_ERR_SUP,"only P-discontinuous");
  modal->family = family;
  dFunctionReturn(0);
}

static dErr dJacobiSetFromOptions_Modal(dJacobi jac)
{
  dJacobiModalFamily family = dJACOBI_MODAL_P_DISCONTINUOUS;
  dErr err;

  dFunctionBegin;
  err = PetscOptionsHead("Tensor options");dCHK(err);
  {
    err = PetscOptionsEnum("-djac_modal_family","Family of modal element","dJacobiModalSetFamily",dJacobiModalFamilies,family,(PetscEnum*)&family,NULL);dCHK(err);
    err = dJacobiModalSetFamily(jac,family);dCHK(err);
  }
  err = PetscOptionsTail();dCHK(err);
  dFunctionReturn(0);
}

static dErr dJacobiGetNodeCount_Modal(dJacobi dUNUSED jac,dInt count,const dEntTopology top[],const dInt deg[],dInt inode[],dInt xnode[])
{
  dErr err;

  dFunctionBegin;
  for (dInt i=0; i<count; i++) {
    dInt n,dd = dMaxInt(0,deg[3*i+0]-1),type = iMesh_TypeFromTopology[top[i]];
    if ((type > 1 && deg[3*i+1] != dd) || (type > 2 && deg[3*i+2] != dd))
      dERROR(PETSC_ERR_ARG_INCOMP,"Degree must be isotropic for P-family elements");
    err = ModalPCount(type,deg[i]-1,&n);dCHK(err);
    if (inode) inode[i] = n;
    if (xnode) xnode[i] = n;
  }
  dFunctionReturn(0);
}

static dErr dJacobiGetConstraintCount_Modal(dUNUSED dJacobi jac,dInt nx,const dInt xi[],const dUNUSED dInt xs[],const dInt dUNUSED is[],
                                             const dInt dUNUSED deg[],dMeshAdjacency ma,dInt nnz[],dInt pnnz[])
{

  dFunctionBegin;
  for (dInt i=0; i<nx; i++) {
    const dInt ei = xi[i];
    switch (ma->topo[ei]) {
      default:
        for (dInt j=xs[i]; j<xs[i+1]; j++) {
          nnz[j] = pnnz[j] = 1;
        }
    }
  }
  dFunctionReturn(0);
}

static dErr dJacobiAddConstraints_Modal(dJacobi dUNUSED jac,dInt nx,const dInt xi[],const dInt xs[],const dInt is[],const dInt dUNUSED deg[],dMeshAdjacency dUNUSED ma,Mat matE,Mat matEp)
{
  dErr err;

  dFunctionBegin;
  for (dInt elem=0; elem<nx; elem++) {
    const dInt ei = xi[elem]; /* Element index, \a is, \a deg and everything in \a ma is addressed by \a ei. */
    if (xs[ei+1]-xs[ei] != is[ei+1]-is[ei])
      dERROR(PETSC_ERR_PLIB,"Different number of interior and expanded nodes with discontinuous element");
    for (dInt i=xs[ei],j=is[ei]; i<xs[i+1]; i++,j++) {
      err = MatSetValue(matE,i,j,1.0,INSERT_VALUES);dCHK(err);
      if (matEp != matE) {
        err = MatSetValue(matEp,i,j,1.0,INSERT_VALUES);dCHK(err);
      }
    }
  }
  dFunctionReturn(0);
}

static dErr dJacobiGetEFS_Modal(dJacobi jac,dInt n,const dEntTopology topo[],const dPolynomialOrder order[],const dRule rules[],dEFS efs[])
{
  dJacobi_Modal *modal = jac->data;
  dErr          err;

  dFunctionBegin;
  for (dInt i=0; i<n; i++) {
    int new;
    khint64_t key = 1;          /* FIXME */
    khiter_t kiter = kh_put_efs(modal->efs,key,&new);
    if (new) {
      dEFS_Modal *newefs;
      dReal      rcoord[256][3];
      dInt       rdim,rsize;
      err = dRuleGetSize(rules[i],&rdim,&rsize);dCHK(err);
      err = dRuleGetNodeWeight(rules[i],&rcoord[0][0],NULL);dCHK(err);
      err = dNewLog(jac,dEFS_Modal,&newefs);dCHK(err);
      newefs->topo = topo[i];
      newefs->rule = rules[i];
      switch (topo[i]) {
        case dTOPO_LINE:
          if (rdim != 1) dERROR(1,"Incompatible Rule dim %d, expected 1",rdim);
          newefs->ops = *modal->efsOpsLine;
          err = dJacobiModalGetBasis(jac,dTOPO_LINE,rsize,rcoord[0],dPolynomialOrderMax(order[i]),&newefs->basis);dCHK(err);
          break;
        case dTOPO_QUAD:
          if (rdim != 2) dERROR(1,"Incompatible Rule dim %d, expected 2",rdim);
          newefs->ops = *modal->efsOpsQuad;
          err = dJacobiModalGetBasis(jac,dTOPO_QUAD,rsize,rcoord[0],dPolynomialOrderMax(order[i]),&newefs->basis);dCHK(err);
          break;
        case dTOPO_HEX:
          if (rdim != 3) dERROR(1,"Incompatible Rule size %d, expected 3",rdim);
          newefs->ops = *modal->efsOpsHex;
          err = dJacobiModalGetBasis(jac,dTOPO_HEX,rsize,rcoord[0],dPolynomialOrderMax(order[i]),&newefs->basis);dCHK(err);
          break;
        default:
          dERROR(1,"no basis available for given topology");
      }
      kh_val(modal->efs,kiter) = newefs;
    }
    efs[i] = (dEFS)kh_val(modal->efs,kiter);
  }
  dFunctionReturn(0);
}

dErr dJacobiCreate_Modal(dJacobi jac)
{
  static const struct _dJacobiOps myops = {
    .SetFromOptions     = dJacobiSetFromOptions_Modal,
    .Destroy            = dJacobiDestroy_Modal,
    .View               = dJacobiView_Modal,
    .PropogateDown      = 0,
    .GetEFS             = dJacobiGetEFS_Modal,
    .GetNodeCount       = dJacobiGetNodeCount_Modal,
    .GetConstraintCount = dJacobiGetConstraintCount_Modal,
    .AddConstraints     = dJacobiAddConstraints_Modal,
  };
  dJacobi_Modal *modal;
  dErr err;

  dFunctionBegin;
  err = dMemcpy(jac->ops,&myops,sizeof myops);dCHK(err);
  err = dNewLog(jac,dJacobi_Modal,&modal);dCHK(err);
  jac->data = modal;

  err = PetscObjectComposeFunctionDynamic((PetscObject)jac,"dJacobiModalSetFamily_C","dJacobiModalSetFamily_Modal",dJacobiModalSetFamily_Modal);dCHK(err);

  err = dJacobiEFSOpsSetUp_Modal(jac);dCHK(err);
  dFunctionReturn(0);
}

dErr dJacobiModalSetFamily(dJacobi jac,dJacobiModalFamily fam)
{
  dErr err,(*f)(dJacobi,dJacobiModalFamily);

  dFunctionBegin;
  err = PetscObjectQueryFunction((PetscObject)jac,"dJacobiModalSetFamily_C",(void (**)(void))&f);dCHK(err);
  if (f) {
    err = (*f)(jac,fam);dCHK(err);
  }
  PetscFunctionReturn(0);
}
