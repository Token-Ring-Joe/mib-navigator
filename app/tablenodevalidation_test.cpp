#include "tablenodevalidation.h"
#include <cstdio>
namespace {int failures=0;void check(bool c,const char*d){if(!c){std::fprintf(stderr,"FAIL: %s\n",d);++failures;}} MibEnvironmentNodeRecord node(MibEnvironmentNodeKind k){MibEnvironmentNodeRecord n;n.kind=k;return n;}}
int main(){MibEnvironmentNodeRecord scalar=node(MibEnvironmentNodeKind::Scalar);const MibEnvironmentNodeRecord*resolved=&scalar;
check(ResolveTableRowNode(nullptr,nullptr,&resolved)==TableNodeValidation::MissingNode&&resolved==nullptr,"null rejected");
check(ResolveTableRowNode(&scalar,nullptr,&resolved)==TableNodeValidation::WrongNodeKind,"scalar rejected");
auto table=node(MibEnvironmentNodeKind::Table),row=node(MibEnvironmentNodeKind::Row),column=node(MibEnvironmentNodeKind::Column);
check(ResolveTableRowNode(&table,nullptr,&resolved)==TableNodeValidation::MissingRowNode,"missing row rejected");
check(ResolveTableRowNode(&row,nullptr,&resolved)==TableNodeValidation::Valid&&resolved==&row,"row accepted");
check(ResolveTableRowNode(&table,&row,&resolved)==TableNodeValidation::Valid&&resolved==&row,"table resolves row");
check(IsValidTableColumnNode(&column)&&!IsValidTableColumnNode(&row),"column classification");
check(IsTableQueryCapableNodeKind(MibEnvironmentNodeKind::Table)&&IsTableQueryCapableNodeKind(MibEnvironmentNodeKind::Row)&&!IsTableQueryCapableNodeKind(MibEnvironmentNodeKind::Column),"query kinds");return failures?1:0;}
