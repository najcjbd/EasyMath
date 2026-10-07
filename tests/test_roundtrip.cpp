#include <cmath>
#include <algorithm>
// 打印-重解析 往返测试: parse(print(ast)) 语义是否等价
#include "expr.hpp"
#include "unicode.hpp"
#include <iostream>
#include <map>
using namespace em;
static bool close(long double a, long double b){ long double d=std::fabs(a-b); return d < 1e-9L*std::max(1.0L,std::max(std::fabs(a),std::fabs(b))); }
int main(){
  const char* cases[] = {
    // 括号优先级回归(这些曾经打印错误)
    "a-(b+c)", "(x^2)^3", "x-(y+z)", "2-(x+1)", "1-(2-x)", "(a-b)-(c-d)",
    "a/(b*c)", "a*(b/c)", "x/(y/z)", "-(x+y)", "-(-x)", "2^-3", "x^(y^z)",
    "2-(x+1)", "2/(x*y)", "a-(b-c)", "a/(b/c)", "-(x+1)", "(x+1)*2", "2^(3^2)",
    "x-(y+z)", "x/(y*z)", "a-(b+c)", "a/(b*c)", "1-(2-x)", "1/(2/x)",
    "x*y-z/w", "(x+y)/(z+w)", "x-(y-z)", "x/(y/z)", "2*x/(y*z)",
    "-(x*y)", "-(x/y)", "(x^2)^3", "2^-3", "-2^2", "(-2)^2", "x^y^z",
    "1/x/y", "1/(x/y)", "(x-y)-(z-w)", "x-(y-(z-w))", "a*b-c*d",
    "sin(x)/(y+1)", "1+2*3^4", "-(2-x)", "a*(b-c)", "a/(b-c)",
    "2*(x+y)^2", "(x+1)/(x-1)", "x^(1/2)", "1/(1+1/x)", "abs(x-y)/(x+y)",
    // 小数/分数当指数、底数、除数时的括号回归(曾经打印成 2^1/2, 1/1/2)
    "2^0.5", "1/0.5", "0.25^0.5", "2^(-0.5)", "0.5^2", "0.5^0.5", "1-0.5",
    "0.5/2", "2/0.5", "0.5*4", "4*0.5", "(0.5)^2", "0.5^0.25+1", "x^0.5",
    "1/(0.5)", "1.5/0.5", "0.1+0.2", "3^0.5*2", "2^0.5^2"
  };
  int bad=0;
  for (auto c : cases) {
    std::string norm = normalize_math(c);
    ParseOptions po; po.declared = scanDeclaredNames(norm);
    std::string err;
    NodePtr a = parseExpression(norm, po, err);
    if(!a){ std::cout<<"PARSE1 FAIL "<<c<<" : "<<err<<"\n"; bad++; continue; }
    std::string printed = astPlain(a);
    std::string norm2 = normalize_math(printed);
    NodePtr b = parseExpression(norm2, po, err);
    if(!b){ std::cout<<"REPARSE FAIL ["<<c<<"] -> ["<<printed<<"] : "<<err<<"\n"; bad++; continue; }
    // 在若干随机点上比较数值
    bool ok=true; std::string detail;
    for (long double x : {-3.5L, 0.7L, 1.3L, 2.25L}) {
      for (long double y : {0.5L, -1.5L, 2.0L}) {
        std::map<std::string,long double> env{{"x",x},{"y",y},{"z",0.3L},{"w",-0.7L},{"a",x},{"b",y},{"c",0.3L},{"d",-0.7L}};
        long double va=0,vb=0; std::string e1,e2;
        bool o1=evalApprox(a,env,va,e1), o2=evalApprox(b,env,vb,e2);
        if(o1!=o2){ ok=false; detail="eval-differs"; break; }
        if(o1 && !close(va,vb)){ ok=false; detail="value "+formatLongDouble(va,12)+" vs "+formatLongDouble(vb,12); break; }
      }
      if(!ok) break;
    }
    if(!ok){ std::cout<<"ROUNDTRIP MISMATCH ["<<c<<"] -> ["<<printed<<"]  "<<detail<<"\n"; bad++; }
  }
  std::cout<<"往返测试: "<<(sizeof(cases)/sizeof(char*))<<" 例, 失败 "<<bad<<"\n";
  return bad?1:0;
}
