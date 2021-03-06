#include <dohp.h>
#include <dohpmesh.h>
#include "tensorquad.h"
#include "../../../impls/tensor/polylib.h"

dErr TensorRuleView(TensorRule rule,PetscViewer viewer)
{
  dBool  ascii;
  dErr err;

  dFunctionBegin;
  err = PetscTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&ascii);dCHK(err);
  if (!ascii) dFunctionReturn(0);
  err = PetscViewerASCIIPrintf(viewer,"TensorRule with %d nodes.\n",rule->size);dCHK(err);
  err = dRealTableView(1,rule->size,rule->coord,viewer,"q");dCHK(err);
  err = dRealTableView(1,rule->size,rule->weight,viewer,"w");dCHK(err);
  dFunctionReturn(0);
}

static dErr TensorRuleDestroy(TensorRule *rule)
{
  dErr err;

  dFunctionBegin;
  if (!*rule) dFunctionReturn(0);
  err = dFree2((*rule)->weight,(*rule)->coord);dCHK(err);
  err = dFree(*rule);dCHK(err);
  dFunctionReturn(0);
}

/* This helper is really a hack. */
static void two_point_private(double nodes[],double weights[],int npoints,double dUNUSED alpha,double dUNUSED beta,double x0,double x1)
{
  int nelems = npoints/2;
  double intervals[nelems+1],unused[nelems+1];
  zwglj(intervals,unused,nelems+1,0,0); /* Get Gauss-Lobatto points (this part is the horrible hack) */
  for (int i=0; i<nelems; i++) {
    double h = intervals[i+1] - intervals[i];
    nodes[2*i+0] = intervals[i] + (x0 - (-1))*h/2.;
    nodes[2*i+1] = intervals[i] + (x1 - (-1))*h/2.;
    weights[2*i+0] = h/2.;
    weights[2*i+1] = h/2.;
  }
}

static void two_point_legendre(double nodes[],double weights[],int npoints,double alpha,double beta)
{two_point_private(nodes,weights,npoints,alpha,beta,-0.57735026918962573,0.57735026918962573);}
static void two_point_lobatto(double nodes[],double weights[],int npoints,double alpha,double beta)
{two_point_private(nodes,weights,npoints,alpha,beta,-1,1);}

static dErr TensorGetRule(dQuadrature_Tensor *tnsr,dQuadratureMethod method,dInt order,TensorRule *rule)
{
  dErr err;
  int key,new;
  khiter_t kiter;

  dFunctionBegin;
  *rule = 0;
  if (!(0 <= order && order < 100)) dERROR(PETSC_COMM_SELF,1,"rule order %d out of bounds",order);
  key = ((uint32_t)method) << 8 | order;
  kiter = kh_put_tensor(tnsr->tensor,key,&new);
  if (new) {
    TensorRule r;
    void (*nodes_and_weights)(double[],double[],int,double,double);
    dInt size;

    switch (method) {
    case dQUADRATURE_METHOD_SPARSE:
      /* The meaning of this option is somewhat unfortunate, but I don't see a clearly better API at the moment.  Here
       * we just assume a Legendre-Gauss-Lobatto tensor product basis, and construct a quadrature (either Gauss or
       * Lobatto) on the patches.  A better system would somehow have the subelement details available explicitly.
       */
      size = order > 0 ? order : 1;
      if (tnsr->alpha != 0 || tnsr->beta != 0) dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"only alpha=0, beta=0 (Legendre)");
      switch (tnsr->family) {
      case dGAUSS_LEGENDRE: nodes_and_weights = two_point_legendre; break;
      case dGAUSS_LOBATTO: nodes_and_weights = two_point_lobatto; break;
      default: dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"GaussFamily %s and dQuadratureMethod %s",dGaussFamilies[tnsr->family],dQuadratureMethods[method]);
      }
      break;
    case dQUADRATURE_METHOD_FAST:
      switch (tnsr->family) {
      case dGAUSS_LEGENDRE:
        size = 1 + order/2;       /* Gauss integrates a polynomial of order 2*size-1 exactly */
        nodes_and_weights = zwgj;
        break;
      case dGAUSS_LOBATTO:
        size = 2 + order/2;     /* Lobatto integrates a polynomial of order 2*size-3 exactly */
        nodes_and_weights = zwglj;
        break;
      default: dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"GaussFamily %s and dQuadratureMethod %s",dGaussFamilies[tnsr->family],dQuadratureMethods[method]);
      }
      break;
    case dQUADRATURE_METHOD_SELF:
      switch (tnsr->family) {
      case dGAUSS_LOBATTO:
        size = 1 + order/2;
        nodes_and_weights = zwglj;
        break;       /* Note that this does not integrate the mass matrix exactly. */
      default: dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"GaussFamily %s and dQuadratureMethod %s",dGaussFamilies[tnsr->family],dQuadratureMethods[method]);
      }
      break;
    default: dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"quadrature method %d",method);
    }
    err = dNew(struct s_TensorRule,&r);dCHK(err);
    err = dMallocA2(size,&r->weight,size,&r->coord);dCHK(err);
    r->size = size;
    if (size == 1) {              /* Polylib function fails for this size. */
      r->weight[0] = 2.0;
      r->coord[0] = 0.0;
    } else {
      nodes_and_weights(r->coord,r->weight,size,tnsr->alpha,tnsr->beta); /* polylib function */
    }
    kh_val(tnsr->tensor,kiter) = r;
  }
  *rule = kh_val(tnsr->tensor,kiter);
  dFunctionReturn(0);
}

static dErr dRulePatchSetup_Tensor(dRule_Tensor *rule,dQuadratureMethod method)
{
  dErr err;
  dInt i,j,k,ii,jj,kk,dim,N[3],Q[3],P[3],NN,PP;
  const dReal *W[3];

  dFunctionBegin;
  err = dRuleGetTensorNodeWeight((dRule)rule,&dim,Q,NULL,W);dCHK(err);
  switch (method) {
  case dQUADRATURE_METHOD_SPARSE: /* Tile the element with 2*2*2 quadrature point patches */
    for (i=0; i<3; i++) {
      P[i] = Q[i] > 1 ? 2 : 1;
      N[i] = Q[i] / P[i];
      if (N[i]*P[i] != Q[i]) dERROR(PETSC_COMM_SELF,PETSC_ERR_PLIB,"Trying to use sparse quadrature, but the number of points %D is not even",Q[i]);
    }
    break;
  default:                      /* Use one big patch for the whole element */
    N[0] = N[1] = N[2] = 1;
    err = dMemcpy(P,Q,sizeof(Q));dCHK(err);
  }
  rule->npatches = NN = N[0]*N[1]*N[2];          /* Number of patches in this element */
  rule->patchsize = PP = P[0]*P[1]*P[2];          /* Number of nodes per patch */
  err = dMallocA2(NN*PP,&rule->patchind,NN*PP,&rule->patchweight);dCHK(err);
  /* 3 nested loops over the patches */
  for (i=0; i<N[0]; i++) {
    for (j=0; j<N[1]; j++) {
      for (k=0; k<N[2]; k++) {
        /* 3 nested loops over quadrature points on each patch */
        for (ii=0; ii<P[0]; ii++) {
          for (jj=0; jj<P[1]; jj++) {
            for (kk=0; kk<P[2]; kk++) {
              const dInt
                patch    = (i*N[1]+j)*N[2]+k,    /* Patch number within element */
                patchidx = (ii*P[1]+jj)*P[2]+kk, /* Index of node within current patch */
                ielem    = i*P[0]+ii,
                jelem    = j*P[1]+jj,
                kelem    = k*P[2]+kk,
                elemidx  = (ielem*Q[1] + jelem)*Q[2] + kelem; /* Index of node within current element */
              rule->patchind[patch*PP+patchidx] = elemidx;
              rule->patchweight[patch*PP+patchidx] = (rule->trule[0]->weight[ielem]
                                                     * (dim < 2 ? 1 : rule->trule[1]->weight[jelem])
                                                     * (dim < 3 ? 1 : rule->trule[2]->weight[jelem]));
            }
          }
        }
      }
    }
  }
  dFunctionReturn(0);
}

static dErr dQuadratureGetRules_Tensor_Private(dQuadrature quad,dInt n,const dEntTopology topo[],const dPolynomialOrder order[],dRule rules[],dQuadratureMethod method)
{
  dQuadrature_Tensor *tnsr = quad->data;
  dErr err;

  dFunctionBegin;
  for (dInt i=0; i<n; i++) {
    int       new;
    khint64_t key   = ((uint64_t)topo[i]) << 32 | ((uint64_t)dPolynomialOrderKeyU32(order[i]));
    khiter_t   kiter = kh_put_rule(tnsr->rules,key,&new);
    if (new) {
      dRule_Tensor *newrule;
      err = dNewLog(quad,dRule_Tensor,&newrule);dCHK(err);
      newrule->topo = topo[i];
      switch (topo[i]) {
        case dTOPO_LINE:
          err = dMemcpy(&newrule->ops,tnsr->ruleOpsLine,sizeof(struct _dRuleOps));dCHK(err);
          err = TensorGetRule(tnsr,method,dPolynomialOrder1D(order[i],0),&newrule->trule[0]);dCHK(err);
          break;
        case dTOPO_QUAD:
          err = dMemcpy(&newrule->ops,tnsr->ruleOpsQuad,sizeof(struct _dRuleOps));dCHK(err);
          err = TensorGetRule(tnsr,method,dPolynomialOrder1D(order[i],0),&newrule->trule[0]);dCHK(err);
          err = TensorGetRule(tnsr,method,dPolynomialOrder1D(order[i],1),&newrule->trule[1]);dCHK(err);
          break;
        case dTOPO_HEX:
          err = dMemcpy(&newrule->ops,tnsr->ruleOpsHex,sizeof(struct _dRuleOps));dCHK(err);
          err = TensorGetRule(tnsr,method,dPolynomialOrder1D(order[i],0),&newrule->trule[0]);dCHK(err);
          err = TensorGetRule(tnsr,method,dPolynomialOrder1D(order[i],1),&newrule->trule[1]);dCHK(err);
          err = TensorGetRule(tnsr,method,dPolynomialOrder1D(order[i],2),&newrule->trule[2]);dCHK(err);
          break;
        default: dERROR(PETSC_COMM_SELF,1,"no rule available for given topology %s",dMeshEntTopologyName(topo[i]));
      }
      err = dRulePatchSetup_Tensor(newrule,method);dCHK(err);
      kh_val(tnsr->rules,kiter) = newrule;
    }
    rules[i] = (dRule)kh_val(tnsr->rules,kiter);
  }
  dFunctionReturn(0);
}

static dErr dQuadratureGetRules_Tensor_FAST(dQuadrature quad,dInt n,const dEntTopology topo[],const dPolynomialOrder order[],dRule rules[])
{return dQuadratureGetRules_Tensor_Private(quad,n,topo,order,rules,dQUADRATURE_METHOD_FAST);}
static dErr dQuadratureGetRules_Tensor_SPARSE(dQuadrature quad,dInt n,const dEntTopology topo[],const dPolynomialOrder order[],dRule rules[])
{return dQuadratureGetRules_Tensor_Private(quad,n,topo,order,rules,dQUADRATURE_METHOD_SPARSE);}
static dErr dQuadratureGetRules_Tensor_SELF(dQuadrature quad,dInt n,const dEntTopology topo[],const dPolynomialOrder order[],dRule rules[])
{return dQuadratureGetRules_Tensor_Private(quad,n,topo,order,rules,dQUADRATURE_METHOD_SELF);}

/*
 * Transformed rules
 */

static dErr dRuleGetSize_Transformed(dRule rule,dInt *dim,dInt *nnodes)
{return dRuleGetSize(((dRule_Transformed*)rule)->reference,dim,nnodes);}
static dErr dRuleGetNodeWeight_Transformed(dRule grule,dReal nodes[],dReal weights[])
{
  dRule_Transformed *rule = (dRule_Transformed*)grule;
  dRule refrule = rule->reference;
  dInt dim,n;
  dErr err;

  dFunctionBegin;
  err = dRuleGetNodeWeight(refrule,nodes,weights);dCHK(err);
  err = dRuleGetSize(refrule,&dim,&n);dCHK(err);
  for (dInt i=0; i<n; i++) {
    dReal x = nodes[3*i+0],y = nodes[3*i+1],z = nodes[3*i+2];
    const dReal (*jac)[3] = (const dReal (*)[3])rule->jac;
    nodes[3*i+0] = jac[0][0]*x + jac[0][1]*y + jac[0][2]*z + rule->translation[0];
    nodes[3*i+1] = jac[1][0]*x + jac[1][1]*y + jac[1][2]*z + rule->translation[1];
    nodes[3*i+2] = jac[2][0]*x + jac[2][1]*y + jac[2][2]*z + rule->translation[2];
  }
  dFunctionReturn(0);
}
static dErr dRuleGetTensorNodeWeight_Transformed(dRule grule,dInt *dim,dInt P[3],const dReal *nodes[3],const dReal *weight[3])
{
  dRule_Transformed *rule = (dRule_Transformed*)grule;
  dRule refrule = rule->reference;
  dInt refP[3];
  const dReal *refnodes[3],*refweight[3];
  dErr err;

  dFunctionBegin;
  err = dRuleGetTensorNodeWeight(refrule,dim,refP,refnodes,refweight);dCHK(err);
  if (!rule->has_tensor) {
    for (dInt i=0; i<3; i++) {
      TensorRule tensor;
      const dReal (*jac)[3] = (const dReal (*)[3])rule->jac;
      if ((!!jac[i][0]) + (!!jac[i][1]) + (!!jac[i][2]) > 1)
        dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"Cannot use this rotation with a tensor product");
      err = dNew(struct s_TensorRule,&tensor);dCHK(err);
      for (dInt j=0; j<3; j++) {
        if (jac[i][j]) {
          err = dMallocA2(refP[j],&tensor->coord,refP[j],&tensor->weight);dCHK(err);
          tensor->size = refP[j];
          for (dInt k=0; k<refP[j]; k++) {
            /* assume that jac[i][j] \in {1,-1}, consistent with present use */
            tensor->coord[k] = jac[i][j] * refnodes[j][k];
            tensor->weight[k] = refweight[j][k];
          }
        }
        goto tensor_done;
      }
      /* This output direction is empty */
      dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"What to do here?");
      tensor_done:
      rule->trule[i] = tensor;
    }
  }
  for (dInt i=0; i<3; i++) {
    P[i]      = rule->trule[i]->size;
    nodes[i]  = rule->trule[i]->coord;
    weight[i] = rule->trule[i]->weight;
  }
  dFunctionReturn(0);
}

/** Create a rule obtained by applying a transform (rotation + translation) to a lower-dimensional rule.
 *
 * @param refrule Lower dimensional tensor product rule
 * @param jac Rotation matrix (only the first \a dim columns will be used, where \a dim is the topological dimension of the Rule)
 */
static dErr dQuadratureCreateTransformedRule(dQuadrature quad,dRule refrule,const dReal jac[9],const dReal translation[3],dRule *newrule)
{
  dRule_Transformed *rule;
  dErr err;

  dFunctionBegin;
  *newrule = NULL;
  err = dNewLog(quad,dRule_Transformed,&rule);dCHK(err);
  rule->ops.getSize             = dRuleGetSize_Transformed;
  rule->ops.getNodeWeight       = dRuleGetNodeWeight_Transformed;
  rule->ops.getTensorNodeWeight = dRuleGetTensorNodeWeight_Transformed;
  rule->reference = refrule;
  err = dMemcpy(rule->jac,jac,9*sizeof(dReal));dCHK(err);
  err = dMemcpy(rule->translation,translation,3*sizeof(dReal));dCHK(err);
  *newrule = (dRule)rule;
  dFunctionReturn(0);
}

static dErr dQuadratureGetFacetRules_Tensor_Private(dQuadrature quad,dInt n,const dEntTopology topo[],const dInt facet[],const dPolynomialOrder order[],dRule rules[],dQuadratureMethod method)
{
  dQuadrature_Tensor *tnsr = quad->data;
  dErr err;

  dFunctionBegin;
  for (dInt i=0; i<n; i++) {
    int new;
    khu_facetrulekey_t key = {.topo = topo[i], .facet = facet[i], .degree = order[i]};
    khiter_t kiter = kh_put_facetrule(tnsr->facetrules,key,&new);
    if (new) {
      dRule newrule;
      switch (topo[i]) {
        case dTOPO_HEX: {
          /* Transform from reference coordinates (X_ref in [-1,1]^2) to physical coordinates by
           * x_phys = jac(:,:end-1) * X_ref + translation
           */
          struct { dReal jac[3][3], translation[3]; } transform[6] = {
            {.jac = {{1,0,0},{0,0,-1},{0,1,0}}, .translation = {0,-1,0}},
            {.jac = {{0,0,1},{1,0,0},{0,1,0}}, .translation = {1,0,0}},
            {.jac = {{-1,0,0},{0,0,1},{0,1,0}}, .translation = {0,1,0}},
            {.jac = {{0,0,-1},{-1,0,0},{0,1,0}}, .translation = {-1,0,0}},
            {.jac = {{0,1,0},{1,0,0},{0,0,-1}}, .translation = {0,0,-1}},
            {.jac = {{1,0,0},{0,1,0},{0,0,1}}, .translation = {0,0,1}},
          },t = transform[facet[i]];
          dInt
            maxdeg = dPolynomialOrderMax(order[i]),
            xdeg = dPolynomialOrder1D(order[i],0),
            ydeg = dPolynomialOrder1D(order[i],1),
            zdeg = dPolynomialOrder1D(order[i],2);
          dPolynomialOrder rotdeg = dPolynomialOrderCreate(maxdeg,
                                            (!!t.jac[0][0])*xdeg+(!!t.jac[0][1])*ydeg+(!!t.jac[0][2])*zdeg,
                                            (!!t.jac[1][0])*xdeg+(!!t.jac[1][1])*ydeg+(!!t.jac[1][2])*zdeg,
                                            (!!t.jac[2][0])*xdeg+(!!t.jac[2][1])*ydeg+(!!t.jac[2][2])*zdeg);
          dEntTopology topoquad = dTOPO_QUAD;
          dRule refrule;
          err = dQuadratureGetRules_Tensor_Private(quad,1,&topoquad,&rotdeg,&refrule,method);dCHK(err);
          err = dQuadratureCreateTransformedRule(quad,refrule,&t.jac[0][0],t.translation,&newrule);dCHK(err);
        } break;
        default: dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"Unsupported topology type '%s'",dMeshEntTopologyName(topo[i]));
      }
      kh_val(tnsr->facetrules,kiter) = newrule;
    }
    rules[i] = kh_val(tnsr->facetrules,kiter);
  }
  dFunctionReturn(0);
}

static dErr dQuadratureGetFacetRules_Tensor_FAST(dQuadrature quad,dInt n,const dEntTopology topo[],const dInt facets[],const dPolynomialOrder order[],dRule rules[])
{return dQuadratureGetFacetRules_Tensor_Private(quad,n,topo,facets,order,rules,dQUADRATURE_METHOD_FAST);}
static dErr dQuadratureGetFacetRules_Tensor_SPARSE(dQuadrature quad,dInt n,const dEntTopology topo[],const dInt facets[],const dPolynomialOrder order[],dRule rules[])
{return dQuadratureGetFacetRules_Tensor_Private(quad,n,topo,facets,order,rules,dQUADRATURE_METHOD_SPARSE);}
static dErr dQuadratureGetFacetRules_Tensor_SELF(dQuadrature quad,dInt n,const dEntTopology topo[],const dInt facets[],const dPolynomialOrder order[],dRule rules[])
{return dQuadratureGetFacetRules_Tensor_Private(quad,n,topo,facets,order,rules,dQUADRATURE_METHOD_SELF);}

static dErr dQuadratureDestroy_Tensor(dQuadrature quad)
{
  dQuadrature_Tensor *tnsr = (dQuadrature_Tensor*)quad->data;
  dErr err;
  khiter_t k;

  dFunctionBegin;
  /* Destroy all cached facet rules */
  for (k=kh_begin(tnsr->facetrules); k!=kh_end(tnsr->facetrules); k++) {
    dRule_Transformed *rule;
    if (!kh_exist(tnsr->facetrules,k)) continue;
    rule = (dRule_Transformed*)kh_val(tnsr->facetrules,k);
    err = dFree(rule);dCHK(err);
  }
  kh_destroy_facetrule(tnsr->facetrules);
  /* Destroy all the cached (n-dimensional) rules */
  for (k=kh_begin(tnsr->rules); k!=kh_end(tnsr->rules); k++) {
    dRule_Tensor *rule;
    if (!kh_exist(tnsr->rules,k)) continue;
    rule = kh_val(tnsr->rules,k);
    err = dFree2(rule->patchind,rule->patchweight);dCHK(err);
    err = dFree(rule);dCHK(err);
  }
  kh_destroy_rule(tnsr->rules);
  /* Destroy all cached 1D tensor rules */
  for (k=kh_begin(tnsr->tensor); k!= kh_end(tnsr->tensor); k++) {
    if (!kh_exist(tnsr->tensor,k)) continue;
    err = TensorRuleDestroy(&kh_val(tnsr->tensor,k));dCHK(err);
  }
  kh_destroy_tensor(tnsr->tensor);
  err = dFree(tnsr->ruleOpsLine);dCHK(err);
  err = dFree(tnsr->ruleOpsQuad);dCHK(err);
  err = dFree(tnsr->ruleOpsHex);dCHK(err);
  err = dFree(tnsr);dCHK(err);
  err = PetscObjectComposeFunctionDynamic((PetscObject)quad,"dQuadratureTensorSetGaussFamily_C","",NULL);dCHK(err);
  dFunctionReturn(0);dCHK(err);
}

static dErr dQuadratureView_Tensor(dQuadrature quad,PetscViewer viewer)
{
  dQuadrature_Tensor *tnsr = (dQuadrature_Tensor*)quad->data;
  dErr err;
  dBool  iascii;

  dFunctionBegin;
  err = PetscTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii);dCHK(err);
  if (!iascii) dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"only ASCII");
  err = PetscViewerASCIIPrintf(viewer,"Tensor Quadrature: %s\n",dGaussFamilies[tnsr->family]);dCHK(err);
  err = PetscViewerASCIIPrintf(viewer,"alpha %g  beta %g\n",tnsr->alpha,tnsr->beta);dCHK(err);
  err = PetscViewerASCIIPrintf(viewer,"Tensor rules:\n");dCHK(err);
  err = PetscViewerASCIIPushTab(viewer);dCHK(err);
  for (khiter_t k=kh_begin(tnsr->tensor); k!= kh_end(tnsr->tensor); k++) {
    err = TensorRuleView(kh_val(tnsr->tensor,k),viewer);dCHK(err);
  }
  err = PetscViewerASCIIPopTab(viewer);dCHK(err);
  dFunctionReturn(0);
}

static dErr dQuadratureSetMethod_Tensor(dQuadrature quad,dQuadratureMethod method)
{
  dQuadrature_Tensor *tnsr = quad->data;

  dFunctionBegin;
  tnsr->method = method;
  switch (method) {
    case dQUADRATURE_METHOD_FAST:
      quad->ops->GetRule      = dQuadratureGetRules_Tensor_FAST;
      quad->ops->GetFacetRule = dQuadratureGetFacetRules_Tensor_FAST;
      break;
    case dQUADRATURE_METHOD_SPARSE:
      quad->ops->GetRule      = dQuadratureGetRules_Tensor_SPARSE;
      quad->ops->GetFacetRule = dQuadratureGetFacetRules_Tensor_SPARSE;
      break;
    case dQUADRATURE_METHOD_SELF:
      quad->ops->GetRule      = dQuadratureGetRules_Tensor_SELF;
      quad->ops->GetFacetRule = dQuadratureGetFacetRules_Tensor_SELF;
      break;
    default: dERROR(PETSC_COMM_SELF,PETSC_ERR_SUP,"Quadrature method '%s'",dQuadratureMethods[method]);
  }
  dFunctionReturn(0);
}

dErr dQuadratureTensorSetGaussFamily(dQuadrature quad,dGaussFamily fam)
{
  dErr err;
  dFunctionBegin;
  dValidHeader(quad,dQUADRATURE_CLASSID,1);
  err = PetscTryMethod(quad,"dQuadratureTensorSetGaussFamily_C",(dQuadrature,dGaussFamily),(quad,fam));dCHK(err);
  dFunctionReturn(0);
}

static dErr dQuadratureTensorSetGaussFamily_Tensor(dQuadrature quad,dGaussFamily fam)
{
  dQuadrature_Tensor *tnsr = quad->data;

  dFunctionBegin;
  tnsr->family = fam;
  dFunctionReturn(0);
}

static dErr dQuadratureSetFromOptions_Tensor(dQuadrature quad)
{
  dQuadrature_Tensor *tnsr  = quad->data;
  dQuadratureMethod  method = dQUADRATURE_METHOD_FAST;
  dBool              flg;
  dErr               err;

  dFunctionBegin;
  err = PetscOptionsHead("Quadrature Tensor Options");dCHK(err);
  err = PetscOptionsEnum("-dquad_tensor_method","Quadrature method","dQuadratureSetMethod",dQuadratureMethods,tnsr->method,(PetscEnum*)&method,&flg);dCHK(err);
  if (flg || !quad->ops->GetRule) {
    err = dQuadratureSetMethod(quad,method);dCHK(err);
  }
  err = PetscOptionsEnum("-dquad_tensor_gauss_family","Gauss type","None",dGaussFamilies,tnsr->family,(PetscEnum*)&tnsr->family,NULL);dCHK(err);
  err = PetscOptionsTail();dCHK(err);
  dFunctionReturn(0);
}

dErr dQuadratureCreate_Tensor(dQuadrature quad)
{
  static const struct _dQuadratureOps myops = {
    .View           = dQuadratureView_Tensor,
    .Destroy        = dQuadratureDestroy_Tensor,
    .GetRule        = 0,        /* Does not exist until method is set */
    .SetFromOptions = dQuadratureSetFromOptions_Tensor,
    .SetMethod      = dQuadratureSetMethod_Tensor,
  };
  dQuadrature_Tensor *tnsr;
  dErr err;

  dFunctionBegin;
  err = dMemcpy(quad->ops,&myops,sizeof(myops));dCHK(err);
  err = dNewLog(quad,dQuadrature_Tensor,&tnsr);dCHK(err);
  quad->data = tnsr;

  err = PetscObjectComposeFunctionDynamic((PetscObject)quad,"dQuadratureTensorSetGaussFamily_C","dQuadratureTensorSetGaussFamily_Tensor",dQuadratureTensorSetGaussFamily_Tensor);dCHK(err);

  tnsr->tensor     = kh_init_tensor();
  tnsr->rules      = kh_init_rule();
  tnsr->facetrules = kh_init_facetrule();
  tnsr->family = dGAUSS_LEGENDRE;
  tnsr->alpha = 0.;
  tnsr->beta = 0.;

  err = dQuadratureRuleOpsSetUp_Tensor(quad);dCHK(err);
  dFunctionReturn(0);
}
