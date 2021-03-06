#include <dohpmeshimpl.h>
#include <dohp.h>

dErr dMeshCreate_Serial(dMesh mesh); /* The only exported function */

static dErr dMeshLoad_Serial(dMesh mesh)
{
  char options[dSTR_LEN];
  size_t fnamelen;
  dErr err;
  dMeshESH root;

  dFunctionBegin;
  err = PetscStrlen(mesh->infile,&fnamelen);dCHK(err);
  if (mesh->inoptions) {
    err = PetscSNPrintf(options,sizeof(options),"%s",mesh->inoptions);dCHK(err);
  } else options[0] = 0;
  err = dMeshGetRoot(mesh,&root);dCHK(err);
  iMesh_load(mesh->mi,root,mesh->infile,options,&err,(int)fnamelen,(int)strlen(options));dICHK(mesh->mi,err);
  dFunctionReturn(0);
}

dErr dMeshCreate_Serial(dMesh mesh)
{
  dIInt ierr;

  dFunctionBegin;
  iMesh_newMesh("",&mesh->mi,&ierr,0);dICHK(mesh->mi,ierr);

  mesh->data = 0;
  mesh->ops->view = 0;
  mesh->ops->destroy = 0;
  mesh->ops->setfromoptions = 0;
  mesh->ops->load = dMeshLoad_Serial;
  mesh->ops->tagbcast = 0;
  dFunctionReturn(0);
}
