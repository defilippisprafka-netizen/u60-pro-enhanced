/* Pure, staged USB Ethernet planner. No syscalls, shell strings, vendor APIs or
 * mutation of network/profile state. An executor is deliberately NOT provided.
 *
 * All operations are scoped to this USB attachment. QUIESCE/ENABLE_LOCAL_DHCP
 * mean suppress/allow DHCP service on THIS PORT, never stop the shared Wi-Fi
 * LAN DHCP server. ISOLATE blocks port bridging/forwarding and DHCP delivery;
 * controlled discovery and WAN DHCP may run while forwarding stays isolated.
 *
 * USB attachment_id must change on physical removal/reinsertion, including the
 * same adapter. current_attachment_id identifies the attachment whose role was
 * actually verified. Never reuse a role from a previous physical attachment.
 *
 * AUTO never chooses LAN. Only a completed, verified usable-upstream probe for
 * this exact attachment permits WAN. Negative/unknown/stale probes keep a
 * confirmed role on the same attachment, or remain isolated/unconfirmed.
 *
 * Executors must check urp_plan_compatible() before EVERY step, stop at the
 * first failure, then use urp_make_rollback(). If rollback itself fails, apply
 * urp_make_fail_closed() and report recovery required. Do not claim a role is
 * active until every validation and RELEASE_ISOLATION step succeeds.
 */
#ifndef U60_USB_ROLE_PLAN_H
#define U60_USB_ROLE_PLAN_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define URP_MAX_STEPS 16

enum urp_role { URP_ROLE_NONE=0, URP_ROLE_WAN, URP_ROLE_LAN };
enum urp_request { URP_REQUEST_AUTO=0, URP_REQUEST_WAN, URP_REQUEST_LAN };
enum urp_profile { URP_PROFILE_DIRECT=0, URP_PROFILE_CLASH, URP_PROFILE_TAILSCALE };
enum urp_probe { URP_PROBE_UNKNOWN=0, URP_PROBE_UPSTREAM_VERIFIED, URP_PROBE_NO_UPSTREAM_VERIFIED };
enum urp_status {
 URP_INVALID_INPUT=0, URP_NO_CHANGE, URP_WAIT_DEBOUNCE, URP_WAIT_LINK,
 URP_WAIT_UPSTREAM_CONFIRMATION, URP_CHANGE_PLANNED, URP_DETACH_PLANNED,
 URP_ROLLBACK_PLANNED, URP_RECOVERY_ISOLATED
};
enum urp_step {
 URP_ISOLATE_PORT=1,
 URP_QUIESCE_USB_LOCAL_DHCP,
 URP_STOP_USB_UPSTREAM_DHCP,
 URP_DETACH_USB_MEMBERSHIP,
 URP_VERIFY_NO_FOREIGN_DHCP,
 URP_ATTACH_USB_WAN,
 URP_START_USB_UPSTREAM_DHCP,
 URP_VERIFY_WAN_ADDRESS,
 URP_VERIFY_UPSTREAM_PATH,
 URP_ATTACH_USB_LAN_BRIDGE,
 URP_ENABLE_USB_LAN_DHCP,
 URP_VERIFY_LAN_SERVICE,
 URP_VERIFY_PROFILE_UNCHANGED,
 URP_RELEASE_ISOLATION
};
struct urp_input {
 int attached, link_up;
 uint64_t attachment_id, current_attachment_id;
 enum urp_role current_role;
 int current_role_confirmed;
 enum urp_request request;
 enum urp_profile profile;
 /* Runtime controller increments when the selected Internet profile changes. */
 uint64_t profile_revision;
 enum urp_probe probe;
 uint64_t probe_attachment_id;
 uint64_t probe_completed_ms;
 /* Probe must complete at/after the last observed link transition. */
 uint64_t now_ms, link_changed_ms;
 uint32_t debounce_ms;
 /* Maximum freshness of an upstream probe; zero accepts no cached probes. */
 uint32_t probe_max_age_ms;
};
struct urp_plan {
 enum urp_status status;
 enum urp_role previous_role, target_role;
 enum urp_request request;
 enum urp_profile preserved_profile;
 uint64_t profile_revision, attachment_id;
 int attachment_was_present;
 int role_change_planned;
 int requires_hardware_acceptance;
 int execution_implemented; /* always 0: this header never authorizes writes */
 int recovery_plan;
 enum urp_step steps[URP_MAX_STEPS];
 size_t step_count;
 const char *reason; /* static literal; no credentials or raw command output */
};
static int urp_valid_input(const struct urp_input *in) {
 if(!in)return 0;
 if((in->attached!=0&&in->attached!=1)||(in->link_up!=0&&in->link_up!=1))return 0;
 if(in->current_role<URP_ROLE_NONE||in->current_role>URP_ROLE_LAN)return 0;
 if(in->request<URP_REQUEST_AUTO||in->request>URP_REQUEST_LAN)return 0;
 if(in->profile<URP_PROFILE_DIRECT||in->profile>URP_PROFILE_TAILSCALE)return 0;
 if(in->probe<URP_PROBE_UNKNOWN||in->probe>URP_PROBE_NO_UPSTREAM_VERIFIED)return 0;
 if(in->current_role_confirmed!=0&&in->current_role_confirmed!=1)return 0;
 if(in->attached&&!in->attachment_id)return 0;
 return 1;
}
static enum urp_role urp_current_role(const struct urp_input *in) {
 return in->attached&&in->current_role_confirmed&&
  in->current_attachment_id==in->attachment_id?in->current_role:URP_ROLE_NONE;
}
static int urp_stable_link(const struct urp_input *in) {
 return in->attached&&in->link_up&&in->now_ms>=in->link_changed_ms&&
  in->now_ms-in->link_changed_ms>=in->debounce_ms;
}
static int urp_verified_upstream(const struct urp_input *in) {
 return in->probe==URP_PROBE_UPSTREAM_VERIFIED&&
  in->probe_attachment_id==in->attachment_id&&
  in->probe_completed_ms>=in->link_changed_ms&&
  in->probe_completed_ms<=in->now_ms&&
  in->probe_max_age_ms>0&&
  in->now_ms-in->probe_completed_ms<=in->probe_max_age_ms;
}
static void urp_initialize(const struct urp_input *in,struct urp_plan *out) {
 memset(out,0,sizeof(*out));out->status=URP_INVALID_INPUT;
 out->reason="Invalid role planning input; no operation planned";
 if(!urp_valid_input(in))return;
 out->previous_role=urp_current_role(in);out->target_role=out->previous_role;
 out->request=in->request;out->preserved_profile=in->profile;
 out->profile_revision=in->profile_revision;out->attachment_id=in->attachment_id;
 out->attachment_was_present=in->attached;
}
static void urp_add(struct urp_plan *out,enum urp_step step) {
 if(out->step_count<URP_MAX_STEPS)out->steps[out->step_count++]=step;
 /* Every plan containing port effects requires real hardware acceptance. */
 out->requires_hardware_acceptance=1;
}
static void urp_isolate_sequence(struct urp_plan *out) {
 urp_add(out,URP_ISOLATE_PORT);
 urp_add(out,URP_QUIESCE_USB_LOCAL_DHCP);
 urp_add(out,URP_STOP_USB_UPSTREAM_DHCP);
 urp_add(out,URP_DETACH_USB_MEMBERSHIP);
}
static void urp_target_sequence(struct urp_plan *out,enum urp_role role) {
 out->target_role=role;
 if(role==URP_ROLE_WAN) {
  urp_add(out,URP_ATTACH_USB_WAN);
  urp_add(out,URP_START_USB_UPSTREAM_DHCP);
  urp_add(out,URP_VERIFY_WAN_ADDRESS);
  urp_add(out,URP_VERIFY_UPSTREAM_PATH);
 }else if(role==URP_ROLE_LAN) {
  /* Test in isolation BEFORE bridging into the router LAN or exposing its
   * DHCP server. Manual LAN selection never bypasses this conflict check. */
  urp_add(out,URP_VERIFY_NO_FOREIGN_DHCP);
  urp_add(out,URP_ATTACH_USB_LAN_BRIDGE);
  urp_add(out,URP_ENABLE_USB_LAN_DHCP);
  urp_add(out,URP_VERIFY_LAN_SERVICE);
 }else return;
 urp_add(out,URP_VERIFY_PROFILE_UNCHANGED);
 urp_add(out,URP_RELEASE_ISOLATION);
}
static int urp_make_plan(const struct urp_input *in,struct urp_plan *out) {
 if(!out)return 0;urp_initialize(in,out);if(!urp_valid_input(in))return 0;
 if(!in->attached) {
  /* Physical unplug is handled immediately, never delayed by debounce. Port
   * cleanup must be idempotent if the device already vanished. */
  out->status=URP_DETACH_PLANNED;out->target_role=URP_ROLE_NONE;
  out->reason="Adapter removed; isolate and clean USB-scoped services immediately";
  urp_isolate_sequence(out);return 1;
 }
 if(!in->link_up) {
  out->status=URP_WAIT_LINK;out->target_role=URP_ROLE_NONE;
  out->reason="Carrier unavailable; keep USB port isolated until link stabilizes";
  urp_isolate_sequence(out);return 1;
 }
 if(!urp_stable_link(in)) {
  out->status=URP_WAIT_DEBOUNCE;
  out->reason="Wait for stable carrier; no new role may be activated";
  /* Fresh attachments must not inherit old bridge/DHCP bindings. A confirmed
   * same-attachment role need not be torn down on every observation. */
  if(out->previous_role==URP_ROLE_NONE)urp_isolate_sequence(out);
  return 1;
 }
 enum urp_role target=URP_ROLE_NONE;
 if(in->request==URP_REQUEST_WAN)target=URP_ROLE_WAN;
 else if(in->request==URP_REQUEST_LAN)target=URP_ROLE_LAN;
 else if(urp_verified_upstream(in))target=URP_ROLE_WAN;
 else {
  out->status=URP_WAIT_UPSTREAM_CONFIRMATION;
  out->reason="AUTO has no fresh verified upstream; retain confirmed role or stay isolated, never assume LAN";
  if(out->previous_role==URP_ROLE_NONE)urp_isolate_sequence(out);
  return 1;
 }
 if(target==out->previous_role) {
  out->status=URP_NO_CHANGE;
  out->reason="Requested role already confirmed on this attachment; Internet profile retained";
  return 1;
 }
 out->status=URP_CHANGE_PLANNED;out->role_change_planned=1;
 out->reason="Staged role change only; all steps and real hardware acceptance required";
 urp_isolate_sequence(out);urp_target_sequence(out,target);return 1;
}
static int urp_plan_compatible(const struct urp_plan *plan,const struct urp_input *live) {
 if(!plan||!urp_valid_input(live)||plan->status==URP_INVALID_INPUT)return 0;
 if(plan->attachment_id!=live->attachment_id||plan->attachment_was_present!=live->attached)return 0;
 if(plan->preserved_profile!=live->profile||plan->profile_revision!=live->profile_revision)return 0;
 if(plan->target_role!=URP_ROLE_NONE&&!urp_stable_link(live))return 0;
 return 1;
}
static int urp_make_fail_closed(const struct urp_input *live,struct urp_plan *out) {
 if(!out)return 0;urp_initialize(live,out);if(!urp_valid_input(live))return 0;
 out->status=URP_RECOVERY_ISOLATED;out->recovery_plan=1;
 out->target_role=URP_ROLE_NONE;
 out->reason="Recovery cannot be verified; keep USB isolated and request hardware review";
 urp_isolate_sequence(out);return 1;
}
static int urp_make_rollback(const struct urp_plan *failed,const struct urp_input *live,struct urp_plan *out) {
 if(!out)return 0;urp_initialize(live,out);
 if(!failed||!urp_valid_input(live)||failed->status==URP_INVALID_INPUT)return 0;
 /* Never restore a role onto a replacement adapter, a down link or an
   * attachment that had no previously confirmed role. Keep NEW user-selected
   * Internet profile from live input; rollback never restores old profiles. */
 if(!live->attached||failed->attachment_id!=live->attachment_id||
    !urp_stable_link(live)||failed->previous_role==URP_ROLE_NONE||failed->recovery_plan)
  return urp_make_fail_closed(live,out);
 out->status=URP_ROLLBACK_PLANNED;out->recovery_plan=1;
 out->previous_role=failed->previous_role;
 out->reason="Restore previously confirmed USB role, with fresh verification and current Internet profile preserved";
 urp_isolate_sequence(out);urp_target_sequence(out,failed->previous_role);
 return 1;
}
static const char *urp_step_name(enum urp_step step) {
 switch(step) {
 case URP_ISOLATE_PORT:return "isolate USB forwarding and DHCP delivery";
 case URP_QUIESCE_USB_LOCAL_DHCP:return "quiesce USB-scoped local DHCP";
 case URP_STOP_USB_UPSTREAM_DHCP:return "stop USB upstream DHCP client";
 case URP_DETACH_USB_MEMBERSHIP:return "detach previous USB WAN or bridge membership";
 case URP_VERIFY_NO_FOREIGN_DHCP:return "verify no foreign DHCP server on isolated USB port";
 case URP_ATTACH_USB_WAN:return "attach USB as WAN";
 case URP_START_USB_UPSTREAM_DHCP:return "acquire USB upstream address";
 case URP_VERIFY_WAN_ADDRESS:return "verify acquired WAN address and gateway";
 case URP_VERIFY_UPSTREAM_PATH:return "verify usable upstream path";
 case URP_ATTACH_USB_LAN_BRIDGE:return "attach USB to LAN bridge";
 case URP_ENABLE_USB_LAN_DHCP:return "enable existing LAN DHCP delivery to USB port";
 case URP_VERIFY_LAN_SERVICE:return "verify USB LAN address service";
 case URP_VERIFY_PROFILE_UNCHANGED:return "verify selected Internet profile remains unchanged";
 case URP_RELEASE_ISOLATION:return "release isolation only after all verification succeeds";
 default:return "unknown step";
 }
}
#endif
