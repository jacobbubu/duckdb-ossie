/** Compare complete fixed-fixture business results across two independently built environments. */
import assert from "node:assert/strict";
import {readFileSync,writeFileSync} from "node:fs";
import {createHash} from "node:crypto";
const [macPath,linuxPath,outputPath]=process.argv.slice(2);
assert(macPath && linuxPath && outputPath && process.argv.length===5);
const hash=(p:string)=>createHash("sha256").update(readFileSync(p)).digest("hex");
const [mac,linux]=[macPath,linuxPath].map(p=>JSON.parse(readFileSync(p,"utf8")));
assert.notEqual(mac.artifact_sha256,linux.artifact_sha256);
for(const key of ["inputs_sha256","implementation_sources_sha256","script_sha256","helper_sha256"])
  assert.deepEqual(mac[key],linux[key],key);
const checks:string[]=[];
const sorted=(rows:unknown[])=>rows.map(r=>JSON.stringify(r)).sort();
for(const threshold of ["10","20"]){
  const a=mac.cases[threshold],b=linux.cases[threshold];
  for(const key of ["query","object_ids","aggregate","content_sha256","ontology_sha256","sql"]){
    assert.deepEqual(a[key],b[key],`${threshold}: ${key}`);
    checks.push(`${threshold}: ${key}`);
  }
  for(const key of ["daily_results","grouped_results","drilldown_objects"]){
    assert.deepEqual(sorted(a[key]),sorted(b[key]),`${threshold}: ${key}`);
    checks.push(`${threshold}: complete ${key} multiset`);
  }
}
for(const key of ["boundary_cases","empty_results"]){assert.deepEqual(mac[key],linux[key],key);checks.push(key);}
writeFileSync(outputPath,JSON.stringify({scope:"fixed GAAP fixture across macOS ARM64 and independently built Ubuntu ARM64; no universal determinism or Agent proof",status:"PASS",reports:[macPath,linuxPath].map(p=>({path:p,sha256:hash(p)})),verifier_sha256:hash(import.meta.path),artifact_sha256:{macos:mac.artifact_sha256,linux:linux.artifact_sha256},checks,result_order:"unspecified SQL row order; complete row multisets compared",float:"exact repeated fixture values observed here; general Float remains approximate"},null,2)+"\n");
