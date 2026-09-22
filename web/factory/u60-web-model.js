/* Shared, side-effect-free schema handling; strings are rendered with text(). */
(function(root,factory){if(typeof define==='function'&&define.amd)define([],factory);else if(typeof module==='object'&&module.exports)module.exports=factory();else root.U60Model=factory();}(this,function(){
 'use strict';
 function interactive(item){return !!item&&item.enabled!==false&&item.type!=='info'&&(item.type==='report'||!!item.action);}
 function argumentsFor(item,choice,fields){
  var result=Object.assign({},item.args||{},choice&&choice.args||{});
  if(fields)(item.fields||[]).forEach(function(f){var v=String(fields[f.key]==null?'':fields[f.key]);if(f.required&&!v)throw new Error('请填写 '+f.label);if(f.kind==='number'&&v&&!isFinite(Number(v)))throw new Error('请输入有效数字');result[f.key]=v;});
  return result;
 }
 function readable(value){if(value===null||typeof value==='undefined'||value==='')return '—';if(typeof value==='boolean')return value?'开启':'关闭';return String(value);}
 function bytes(v,speed){if(typeof v!=='number'||!isFinite(v)||v<0)return '—';var units=speed?['B/s','KB/s','MB/s','GB/s']:['B','KB','MB','GB','TB'],i=0;while(v>=1024&&i<units.length-1){v/=1024;i++;}return v.toFixed(v>=100||i===0?0:1)+' '+units[i];}
 function filterSections(sections,tab){var groups={network:['wifi','usb','router','cell','clients'],clash:['clash'],tailscale:['tailscale'],device:['battery','usage','system'],more:['band','signal','sim','sms','diagnostics']};return (sections||[]).filter(function(s){return groups[tab]&&groups[tab].indexOf(s.id)>=0;});}
 return {interactive:interactive,argumentsFor:argumentsFor,readable:readable,bytes:bytes,filterSections:filterSections};
}));
