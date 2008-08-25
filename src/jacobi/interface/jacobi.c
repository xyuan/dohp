/**
* @file   jacobi.c
* @author Jed Brown <jed@59A2.org>
* @date   Fri Aug 22 14:06:41 2008
* 
* @brief  Compute quadrature rules, interpolation, and differentiation matrices
* 
* 
*/

#include "petsc.h"
#include "dohpjacobi.h"
#include "private/fsimpl.h"

PetscCookie dJACOBI_COOKIE;
static PetscFList dJacobiList = 0;

static const struct _dJacobiOps _defaultOps = {
  .view = 0,
  .setup = 0,
  .setfromoptions = 0,
  .destroy = 0
};

#undef __FUNCT__
#define __FUNCT__ "dJacobiCreate"
/** 
* Create a new Jacobi object and initialize with defaults.
* 
* @param comm 
* @param injacobi 
* 
* @return 
*/
dErr dJacobiCreate(MPI_Comm comm,dJacobi *injacobi)
{
  dJacobi jac;
  dErr err;

  dFunctionBegin;
  dValidPointer(injacobi,2);
  *injacobi = 0;
#if !defined(PETSC_USE_DYNAMIC_LIBRARIES)
  err = dJacobiInitializePackage(PETSC_NULL);dCHK(err);
#endif
  err = PetscHeaderCreate(jac,p_dJacobi,struct _dJacobiOps,dJACOBI_COOKIE,0,"dJacobi",comm,dJacobiDestroy,dJacobiView);dCHK(err);

  jac->basisdegree = 10;
  jac->ruleexcess = 5;
  jac->setupcalled = 0;
  jac->impl = 0;
  err = PetscMemcpy(jac->ops,&_defaultOps,sizeof(struct _dJacobiOps));dCHK(err);

  *injacobi = jac;
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiSetType"
/** 
* Set the type for a dJacobi object.
* 
* @param jac 
* @param type 
* 
* @return 
*/
dErr dJacobiSetType(dJacobi jac,dJacobiType type)
{
  dErr err,(*r)(dJacobi);
  dBool     match;

  dFunctionBegin;
  PetscValidHeaderSpecific(jac,dJACOBI_COOKIE,1);
  PetscValidCharPointer(type,2);
  err = PetscTypeCompare((PetscObject)jac,type,&match);dCHK(err);
  if (match) dFunctionReturn(0);
  err = PetscFListFind(dJacobiList,((PetscObject)jac)->comm,type,(void(**)(void))&r);dCHK(err);
  if (!r) dERROR(1,"Unable to find requested dJacobi type %s",type);
  if (jac->ops->destroy) { err = (*jac->ops->destroy)(jac);dCHK(err); }
  err = PetscMemcpy(jac->ops,&_defaultOps,sizeof(struct _dJacobiOps));dCHK(err);
  jac->setupcalled = 0;
  err = (*r)(jac);dCHK(err);
  err = PetscObjectChangeTypeName((PetscObject)jac,type);dCHK(err);
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiSetFromOptions"
/** 
* Set options from the options database.
* 
* @param jac
* 
* @return 
*/
dErr dJacobiSetFromOptions(dJacobi jac)
{
  char type[dNAME_LEN] = dJACOBI_TENSOR;
  dBool typeSet;
  dErr err;

  dFunctionBegin;
  PetscValidHeaderSpecific(jac,dJACOBI_COOKIE,1);
  err = PetscOptionsBegin(((PetscObject)jac)->comm,((PetscObject)jac)->prefix,"Jacobi options (type and size of basis/quadrature rules)","dJacobi");dCHK(err);
  err = PetscOptionsList("-djac_type","Basis/Quadrature type","dJacobiSetType",dJacobiList,
                          (((PetscObject)jac)->type_name?((PetscObject)jac)->type_name:type),type,dNAME_LEN,&typeSet);dCHK(err);
  if (typeSet) {
    err = dJacobiSetType(jac,type);dCHK(err);
  }
  if (!((PetscObject)jac)->type_name) {
    err = dJacobiSetType(jac,type);dCHK(err);
  }
  err = PetscOptionsInt("-djac_basis_degree","Max basis degree","dJacobiSetDegrees",jac->basisdegree,&jac->basisdegree,PETSC_NULL);dCHK(err);
  err = PetscOptionsInt("-djac_rule_excess","Excess quadrature points","dJacobiSetDegrees",jac->ruleexcess,&jac->ruleexcess,PETSC_NULL);dCHK(err);
  if (jac->ops->setfromoptions) {
    err = jac->ops->setfromoptions(jac);dCHK(err);
  }
  err = PetscOptionsEnd();dCHK(err);
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiSetUp"
/** 
* Initialize the Jacobi object.
* 
* @param jac 
* 
* @return 
*/
dErr dJacobiSetUp(dJacobi jac)
{
  dErr err;

  dFunctionBegin;
  PetscValidHeaderSpecific(jac,dJACOBI_COOKIE,1);
  if (!jac->setupcalled && jac->ops->setup) {
    err = jac->ops->setup(jac);dCHK(err);
  }
  jac->setupcalled = 1;
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiDestroy"
/** 
* Destroy a Jacobi object.
* 
* @param jac 
* 
* @return 
*/
dErr dJacobiDestroy(dJacobi jac)
{
  dErr err;

  dFunctionBegin;
  PetscValidHeaderSpecific(jac,dJACOBI_COOKIE,1);
  if (jac->ops->destroy) {
    err = jac->ops->destroy(jac);dCHK(err);
  }
  err = PetscHeaderDestroy(jac);dCHK(err);
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiView"
/** 
* View the state of a dJacobi.
* 
* @param jac 
* @param viewer 
* 
* @return 
*/
dErr dJacobiView(dJacobi jac,PetscViewer viewer)
{
  dBool iascii;
  dErr err;

  dFunctionBegin;
  PetscValidHeaderSpecific(jac,dJACOBI_COOKIE,1);
  if (!viewer) {
    err = PetscViewerASCIIGetStdout(((PetscObject)jac)->comm,&viewer);dCHK(err);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_COOKIE,2);
  PetscCheckSameComm(jac,1,viewer,2);

  err = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);dCHK(err);
  if (iascii) {
    err = PetscViewerASCIIPrintf(viewer,"dJacobi object:(%s)\n",
                                  ((PetscObject)jac)->prefix ? ((PetscObject)jac)->prefix : "no prefix");dCHK(err);
    err = PetscViewerASCIIPushTab(viewer);dCHK(err);
    err = PetscViewerASCIIPrintf(viewer,"type: %s\n",
                                  ((PetscObject)jac)->type_name ? ((PetscObject)jac)->type_name : "type not set");dCHK(err);
    err = PetscViewerASCIIPrintf(viewer,"max basis degree: %d\n",jac->basisdegree);dCHK(err);
    err = PetscViewerASCIIPrintf(viewer,"rule excess: %d\n",jac->ruleexcess);dCHK(err);
    if (!jac->setupcalled) {
      err = PetscViewerASCIIPrintf(viewer,"Object has not been set up.\n",jac->basisdegree);dCHK(err);
    }
    if (jac->ops->view) {
      err = (*jac->ops->view)(jac,viewer);dCHK(err);
    } else {
      err = PetscViewerASCIIPrintf(viewer,"Internal info not available.\n");dCHK(err);
    }
    err = PetscViewerASCIIPopTab(viewer);dCHK(err);
  } else if (jac->ops->view) {
    err = (*jac->ops->view)(jac,viewer);dCHK(err);
  }
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiRegister"
dErr dJacobiRegister(const char name[],const char path[],const char cname[],dErr(*create)(dJacobi))
{
  char fullname[dMAX_PATH_LEN];
  dErr err;

  dFunctionBegin;
  err = PetscFListConcat(path,cname,fullname);dCHK(err);
  err = PetscFListAdd(&dJacobiList,name,fullname,(void (*)(void))create);dCHK(err); 
  dFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "dJacobiRegisterAll"
dErr dJacobiRegisterAll(const char path[])
{
  static dBool called = PETSC_FALSE;
  dErr err;

  dFunctionBegin;
  err = dJacobiRegisterDynamic(dJACOBI_TENSOR,path,"dJacobiCreate_Tensor",dJacobiCreate_Tensor);dCHK(err);
  called = PETSC_TRUE;
  dFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "dJacobiInitializePackage"
dErr dJacobiInitializePackage(const char path[])
{
  static dBool initialized = PETSC_FALSE;
  dErr err;

  dFunctionBegin;
  if (initialized) dFunctionReturn(0);
  err = PetscCookieRegister("Jacobi context",&dJACOBI_COOKIE);dCHK(err);
  err = dJacobiRegisterAll(path);dCHK(err);
  initialized = PETSC_TRUE;
  dFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "dJacobiSetDegrees"
/** 
* Set the maximum size of the approximation space generated by Jacobi.
*
* Jacobi will always generate quadrature rules up to the maximum order.  This is to save us from a degenerate case where
* one field has low order on an element but another has very high order.  In this case, a quadrature order close to \p
* basisdegree + \p ruleexcess will be required due to the second field.  We will not normally generate quadrature rules
* with fewer points than the number of functions in the basis because this makes the element mass matrix singular.
*
* @param jac The Jacobi context
* @param basisdegree The maximum number of functions in a 1D basis.
* @param ruleexcess The number of extra quadrature points to generate rules for.
* 
* @return 
*/
dErr dJacobiSetDegrees(dJacobi jac,dInt basisdegree,dInt ruleexcess)
{
  dErr err;

  dFunctionBegin;
  PetscValidHeaderSpecific(jac,dJACOBI_COOKIE,1);
  if (jac->setupcalled) {
    if (jac->ops->destroy) { err = (*jac->ops->destroy)(jac);dCHK(err); }
    err = PetscMemcpy(jac->ops,&_defaultOps,sizeof(struct _dJacobiOps));dCHK(err);
  }
  jac->basisdegree = basisdegree;
  jac->ruleexcess = ruleexcess;
  dFunctionReturn(0);
}

/** 
* Writes a new Rule into the buffer pointed to by \a rule.  The number of bytes required is returned in \a bytes.  The
* \c dRule struct has an array of private pointers at the end.  Different topology and/or basis types may need a
* different number of pointers.
*
* @example An anisotropic tensor product rule for a Hexahedron needs 3 data pointers hence \a bytes will be
* 'sizeof(dRule)+3*sizeof(void*)'
* 
* @param jac the context
* @param top topology of the element
* @param rsize number of points in each Cartesian direction
* @param left number of bytes left in \a rule
* @param rule place to put the newly constructed dRule
* @param bytes number of bytes used
* 
* @return err
*/
EXTERN dErr dJacobiGetRule(dJacobi jac,dTopology top,const dInt rsize[],dInt left,dRule *rule,dInt *bytes)
{
  dErr err;

  dFunctionBegin;
  dValidHeader(jac,dJACOBI_COOKIE,1);
  dValidPointer(rsize,3);
  dValidPointer(rule,5);
  dValidPointer(bytes,6);
  err = jac->ops->getrule(jac,top,rsize,left,rule,bytes);dCHK(err);
  dFunctionReturn(0);
}


/** 
* Get a basis context.
* 
* @param jac 
* @param bsize number of basis functions
* @param qsize number of quadrature points
* @param[out] basis basis handle
* 
* @return 
*/
EXTERN dErr dJacobiGetEFS(dJacobi jac,dTopology top,const dInt bsize[],const dRule *rule,dInt left,dEFS *efs,dInt *bytes)
{
  dErr err;

  dFunctionBegin;
  dValidHeader(jac,dJACOBI_COOKIE,1);
  dValidPointer(bsize,3);
  dValidPointer(rule,4);
  dValidPointer(efs,6);
  dValidPointer(bytes,7);
  err = jac->ops->getefs(jac,top,bsize,rule,left,efs,bytes);dCHK(err);
  dFunctionReturn(0);
}
