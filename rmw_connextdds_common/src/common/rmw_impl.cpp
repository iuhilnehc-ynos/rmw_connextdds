// Copyright 2020 Real-Time Innovations, Inc. (RTI)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rmw_connextdds/rmw_impl.hpp"

#include <algorithm>
#include <string>
#include <vector>
#include <stdexcept>

#include "rmw_connextdds/graph_cache.hpp"

#define ROS_SERVICE_REQUESTER_PREFIX_STR "rq"
#define ROS_SERVICE_RESPONSE_PREFIX_STR  "rr"

const char * const ROS_TOPIC_PREFIX = "rt";
const char * const ROS_SERVICE_REQUESTER_PREFIX = ROS_SERVICE_REQUESTER_PREFIX_STR;
const char * const ROS_SERVICE_RESPONSE_PREFIX = ROS_SERVICE_RESPONSE_PREFIX_STR;

static
rmw_ret_t
rmw_connextdds_duration_from_ros_time(
  DDS_Duration_t * const duration,
  const rmw_time_t * const ros_time)
{
  if (ros_time->sec > INT32_MAX || ros_time->nsec > UINT32_MAX) {
    RMW_CONNEXT_LOG_ERROR("duration overflow detected")
    return RMW_RET_ERROR;
  }
  duration->sec = static_cast<DDS_Long>(ros_time->sec);
  duration->nanosec = static_cast<DDS_UnsignedLong>(ros_time->nsec);
  return RMW_RET_OK;
}

std::string
rmw_connextdds_create_topic_name(
  const char * prefix,
  const char * topic_name,
  const char * suffix,
  bool avoid_ros_namespace_conventions)
{
  if (avoid_ros_namespace_conventions) {
    return std::string(topic_name) + std::string(suffix);
  } else {
    return std::string(prefix) +
           std::string(topic_name) +
           std::string(suffix);
  }
}

std::string
rmw_connextdds_create_topic_name(
  const char * prefix,
  const char * topic_name,
  const char * suffix,
  const rmw_qos_profile_t * qos_policies)
{
  return rmw_connextdds_create_topic_name(
    prefix,
    topic_name,
    suffix,
    qos_policies->avoid_ros_namespace_conventions);
}

rcutils_ret_t
rcutils_uint8_array_copy(
  rcutils_uint8_array_t * const dst,
  const rcutils_uint8_array_t * const src)
{
  if (src->buffer_length > 0) {
    if (src->buffer_length > dst->buffer_capacity) {
      rcutils_ret_t rc =
        rcutils_uint8_array_resize(dst, src->buffer_length);

      if (RCUTILS_RET_OK != rc) {
        return rc;
      }
    }

    dst->buffer_length = src->buffer_length;
    memcpy(dst->buffer, src->buffer, src->buffer_length);
  } else {
    dst->buffer_length = 0;
  }

  return RCUTILS_RET_OK;
}


/******************************************************************************
 * Qos Helpers
 ******************************************************************************/
rmw_qos_policy_kind_t
dds_qos_policy_to_rmw_qos_policy(const DDS_QosPolicyId_t last_policy_id)
{
  switch (last_policy_id) {
    case DDS_DURABILITY_QOS_POLICY_ID:
      return RMW_QOS_POLICY_DURABILITY;
    case DDS_DEADLINE_QOS_POLICY_ID:
      return RMW_QOS_POLICY_DEADLINE;
    case DDS_LIVELINESS_QOS_POLICY_ID:
      return RMW_QOS_POLICY_LIVELINESS;
    case DDS_RELIABILITY_QOS_POLICY_ID:
      return RMW_QOS_POLICY_RELIABILITY;
    case DDS_HISTORY_QOS_POLICY_ID:
      return RMW_QOS_POLICY_HISTORY;
    case DDS_LIFESPAN_QOS_POLICY_ID:
      return RMW_QOS_POLICY_LIFESPAN;
    default:
      break;
  }
  return RMW_QOS_POLICY_INVALID;
}

rmw_ret_t
rmw_connextdds_get_readerwriter_qos(
  const bool writer_qos,
  RMW_Connext_MessageTypeSupport * const type_support,
  DDS_HistoryQosPolicy * const history,
  DDS_ReliabilityQosPolicy * const reliability,
  DDS_DurabilityQosPolicy * const durability,
  DDS_DeadlineQosPolicy * const deadline,
  DDS_LivelinessQosPolicy * const liveliness,
  DDS_ResourceLimitsQosPolicy * const resource_limits,
  DDS_PublishModeQosPolicy * const publish_mode,
#if RMW_CONNEXT_HAVE_LIFESPAN_QOS
  DDS_LifespanQosPolicy * const lifespan,
#endif /* RMW_CONNEXT_HAVE_LIFESPAN_QOS */
  const rmw_qos_profile_t * const qos_policies
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  ,
  const rmw_publisher_options_t * const pub_options,
  const rmw_subscription_options_t * const sub_options
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
)
{
  UNUSED_ARG(writer_qos);
  UNUSED_ARG(type_support);
  UNUSED_ARG(publish_mode);
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  UNUSED_ARG(pub_options);
  UNUSED_ARG(sub_options);
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */

  switch (qos_policies->history) {
    case RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT:
      {
        break;
      }
    case RMW_QOS_POLICY_HISTORY_KEEP_LAST:
      {
        if (qos_policies->depth == RMW_QOS_POLICY_DEPTH_SYSTEM_DEFAULT) {
          history->depth = 1;
          history->kind = DDS_KEEP_LAST_HISTORY_QOS;
        } else {
          if (qos_policies->depth < 1 || qos_policies->depth > INT32_MAX) {
            RMW_CONNEXT_LOG_ERROR_A_SET(
              "unsupported history depth: %ld", qos_policies->depth)
            return RMW_RET_ERROR;
          }

          history->depth = static_cast<DDS_Long>(qos_policies->depth);
          history->kind = DDS_KEEP_LAST_HISTORY_QOS;
        }
        break;
      }
    case RMW_QOS_POLICY_HISTORY_KEEP_ALL:
      {
        history->kind = DDS_KEEP_ALL_HISTORY_QOS;
        break;
      }
    // case RMW_QOS_POLICY_HISTORY_UNKNOWN:
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "unsupported history kind: %d", qos_policies->history)
        return RMW_RET_ERROR;
      }
  }

  RMW_CONNEXT_LOG_DEBUG_A(
    "endpoint resource history: "
    "kind=%d, "
    "depth=%d",
    history->kind,
    history->depth);

  reliability->max_blocking_time = DDS_DURATION_INFINITE;

  switch (qos_policies->reliability) {
    case RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT:
      {
        break;
      }
    case RMW_QOS_POLICY_RELIABILITY_RELIABLE:
      {
        reliability->kind = DDS_RELIABLE_RELIABILITY_QOS;
        break;
      }
    case RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT:
      {
        reliability->kind = DDS_BEST_EFFORT_RELIABILITY_QOS;
        break;
      }
    // case RMW_QOS_POLICY_RELIABILITY_UNKNOWN:
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "unsupported reliability kind: %d", qos_policies->reliability)
        return RMW_RET_ERROR;
      }
  }

  switch (qos_policies->durability) {
    case RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT:
      {
        break;
      }
    case RMW_QOS_POLICY_DURABILITY_VOLATILE:
      {
        durability->kind = DDS_VOLATILE_DURABILITY_QOS;
        break;
      }
    case RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL:
      {
        durability->kind = DDS_TRANSIENT_LOCAL_DURABILITY_QOS;
        break;
      }
    // case RMW_QOS_POLICY_DURABILITY_UNKNOWN:
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "unsupported durability kind: %d", qos_policies->durability)
        return RMW_RET_ERROR;
      }
  }

  if (qos_policies->deadline.sec != 0 || qos_policies->deadline.nsec != 0) {
    deadline->period.sec =
      static_cast<DDS_Long>(qos_policies->deadline.sec);
    deadline->period.nanosec =
      static_cast<DDS_UnsignedLong>(qos_policies->deadline.nsec);
  }

  if (qos_policies->liveliness_lease_duration.sec != 0 ||
    qos_policies->liveliness_lease_duration.nsec != 0)
  {
    liveliness->lease_duration.sec =
      static_cast<DDS_Long>(qos_policies->liveliness_lease_duration.sec);
    liveliness->lease_duration.nanosec =
      static_cast<DDS_UnsignedLong>(
      qos_policies->liveliness_lease_duration.nsec);
  }

  switch (qos_policies->liveliness) {
    case RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT:
      {
        break;
      }
    case RMW_QOS_POLICY_LIVELINESS_AUTOMATIC:
      {
        liveliness->kind = DDS_AUTOMATIC_LIVELINESS_QOS;
        break;
      }
    case RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC:
      {
        liveliness->kind = DDS_MANUAL_BY_TOPIC_LIVELINESS_QOS;
        break;
      }
    // case RMW_QOS_POLICY_LIVELINESS_UNKNOWN:
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "unsupported liveliness kind: %d", qos_policies->liveliness)
        return RMW_RET_ERROR;
      }
  }

#if RMW_CONNEXT_HAVE_LIFESPAN_QOS
  if (nullptr != lifespan &&
    (qos_policies->lifespan.sec != 0 || qos_policies->lifespan.nsec != 0))
  {
#if RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_PRO
    lifespan->duration.sec = static_cast<DDS_Long>(qos_policies->lifespan.sec);
    lifespan->duration.nanosec =
      static_cast<DDS_UnsignedLong>(qos_policies->lifespan.nsec);
#else
    RMW_CONNEXT_LOG_WARNING("lifespan qos policy not supported")
#endif /* RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_PRO */
  }
#endif /* RMW_CONNEXT_HAVE_LIFESPAN_QOS */

  // Make sure that resource limits are consistent with history qos
  // TODO(asorbini): do not overwrite if using non-default QoS
  if (history->kind == DDS_KEEP_LAST_HISTORY_QOS &&
    history->depth > 1 &&
    resource_limits->max_samples_per_instance == DDS_LENGTH_UNLIMITED)
  {
    resource_limits->max_samples_per_instance = history->depth;
    if (resource_limits->max_samples != DDS_LENGTH_UNLIMITED &&
      resource_limits->max_samples < resource_limits->max_samples_per_instance)
    {
      resource_limits->max_samples =
        resource_limits->max_samples_per_instance;
    }
  }

  return RMW_RET_OK;
}

rmw_ret_t
rmw_connextdds_readerwriter_qos_to_ros(
  const DDS_HistoryQosPolicy * const history,
  const DDS_ReliabilityQosPolicy * const reliability,
  const DDS_DurabilityQosPolicy * const durability,
  const DDS_DeadlineQosPolicy * const deadline,
  const DDS_LivelinessQosPolicy * const liveliness,
#if RMW_CONNEXT_HAVE_LIFESPAN_QOS
  const DDS_LifespanQosPolicy * const lifespan,
#endif /* RMW_CONNEXT_HAVE_LIFESPAN_QOS */
  rmw_qos_profile_t * const qos_policies)
{
  if (nullptr != history) {
    switch (history->kind) {
      case DDS_KEEP_LAST_HISTORY_QOS:
        {
          qos_policies->history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
          break;
        }
      case DDS_KEEP_ALL_HISTORY_QOS:
        {
          qos_policies->history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
          break;
        }
      default:
        {
          RMW_CONNEXT_LOG_ERROR_A_SET(
            "invalid DDS history kind: %d", history->kind)
          return RMW_RET_ERROR;
        }
    }
    qos_policies->depth = (uint32_t) history->depth;
  }

  switch (reliability->kind) {
    case DDS_RELIABLE_RELIABILITY_QOS:
      {
        qos_policies->reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
        break;
      }
    case DDS_BEST_EFFORT_RELIABILITY_QOS:
      {
        qos_policies->reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
        break;
      }
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "invalid DDS reliability kind: %d", reliability->kind)
        return RMW_RET_ERROR;
      }
  }

  switch (durability->kind) {
    case DDS_VOLATILE_DURABILITY_QOS:
      {
        qos_policies->durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
        break;
      }
    case DDS_TRANSIENT_LOCAL_DURABILITY_QOS:
      {
        qos_policies->durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
        break;
      }
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "invalid DDS durability kind: %d", durability->kind)
        return RMW_RET_ERROR;
      }
  }

  qos_policies->deadline.sec = deadline->period.sec;
  qos_policies->deadline.nsec = deadline->period.nanosec;

  qos_policies->liveliness_lease_duration.sec =
    liveliness->lease_duration.sec;
  qos_policies->liveliness_lease_duration.nsec =
    liveliness->lease_duration.nanosec;

  switch (liveliness->kind) {
    case DDS_AUTOMATIC_LIVELINESS_QOS:
      {
        qos_policies->liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
        break;
      }
    case DDS_MANUAL_BY_TOPIC_LIVELINESS_QOS:
      {
        qos_policies->liveliness = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
        break;
      }
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "invalid DDS liveliness kind: %d", liveliness->kind)
        return RMW_RET_ERROR;
      }
  }

#if RMW_CONNEXT_HAVE_LIFESPAN_QOS
  if (nullptr != lifespan) {
#if RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_PRO
    qos_policies->lifespan.sec = lifespan->duration.sec;
    qos_policies->lifespan.nsec = lifespan->duration.nanosec;
#else
    RMW_CONNEXT_LOG_WARNING("lifespan qos policy not supported")
#endif /* RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_PRO */
  }
#endif /* RMW_CONNEXT_HAVE_LIFESPAN_QOS */

  return RMW_RET_OK;
}


rmw_ret_t
rmw_connextdds_datawriter_qos_to_ros(
  DDS_DataWriterQos * const qos,
  rmw_qos_profile_t * const qos_policies)
{
  return rmw_connextdds_readerwriter_qos_to_ros(
    &qos->history,
    &qos->reliability,
    &qos->durability,
    &qos->deadline,
    &qos->liveliness,
#if RMW_CONNEXT_HAVE_LIFESPAN_QOS
#if RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_PRO
    &qos->lifespan,
#elif RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_MICRO
    nullptr,
#endif /* RMW_CONNEXT_DDS_API == RMW_CONNEXT_DDS_API_PRO */
#endif /* RMW_CONNEXT_HAVE_LIFESPAN_QOS */
    qos_policies);
}


rmw_ret_t
rmw_connextdds_datareader_qos_to_ros(
  DDS_DataReaderQos * const qos,
  rmw_qos_profile_t * const qos_policies)
{
  return rmw_connextdds_readerwriter_qos_to_ros(
    &qos->history,
    &qos->reliability,
    &qos->durability,
    &qos->deadline,
    &qos->liveliness,
#if RMW_CONNEXT_HAVE_LIFESPAN_QOS
    nullptr /* Lifespan is a writer-only qos policy */,
#endif /* RMW_CONNEXT_HAVE_LIFESPAN_QOS */
    qos_policies);
}

bool
rmw_connextdds_find_string_in_list(
  const DDS_StringSeq * const profile_names,
  const char * const profile)
{
  const DDS_Long profiles_len = DDS_StringSeq_get_length(profile_names);
  for (DDS_Long i = 0; i < profiles_len; i++) {
    const char * const profile_str =
      *DDS_StringSeq_get_reference(profile_names, i);

    if (strcmp(profile_str, profile) == 0) {
      return true;
    }
  }
  return false;
}

#define RMW_CONNEXT_QOS_TAG_NODE          "[node]"
#define RMW_CONNEXT_QOS_TAG_PUBLISHER     "[pub]"
#define RMW_CONNEXT_QOS_TAG_SUBSCRIPTION  "[sub]"
#define RMW_CONNEXT_QOS_TAG_CLIENT        "[client]"
#define RMW_CONNEXT_QOS_TAG_SERVICE       "[service]"
#define RMW_CONNEXT_QOS_TAG_REQUEST       "[request]"
#define RMW_CONNEXT_QOS_TAG_REPLY         "[reply]"

rmw_ret_t
rmw_connextdds_list_context_qos_profiles(
  rmw_context_impl_t * const ctx,
  std::vector<std::string> & profiles)
{
  const bool has_lib = ctx->qos_library.size() > 0;
  try {
    std::ostringstream ss;

    if (has_lib) {
      // e.g. "my_lib::/foo/bar/my_ctx",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx"
      ss << ctx->qos_library << "::" << ctx->qos_ctx_name;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/[node]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << RMW_CONNEXT_QOS_TAG_NODE;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::[node]"
      ss << ctx->qos_library << "::" << RMW_CONNEXT_QOS_TAG_NODE;
      profiles.push_back(ss.str());
      ss.str("");
    }

    // e.g. "/foo/bar/my_ctx::[node]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" << RMW_CONNEXT_QOS_TAG_NODE;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/::[node]"
    ss << ctx->qos_ctx_namespace << "::" << RMW_CONNEXT_QOS_TAG_NODE;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << ctx->qos_ctx_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/[node]",
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << RMW_CONNEXT_QOS_TAG_NODE;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::[node]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << RMW_CONNEXT_QOS_TAG_NODE;
    profiles.push_back(ss.str());
  } catch (const std::exception & e) {
    UNUSED_ARG(e);
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

static
rmw_ret_t
rmw_connextdds_list_pubsub_qos_profiles(
  rmw_context_impl_t * const ctx,
  const char * const topic_name,
  const char * const type_tag,
  std::vector<std::string> & profiles)
{
  const bool has_lib = ctx->qos_library.size() > 0;
  try {
    std::ostringstream ss;

    if (has_lib) {
      // e.g. "my_lib::/foo/bar/my_ctx/my_topic[pub]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
        topic_name << type_tag;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/my_ctx/my_topic",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name << topic_name;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx/my_topic[pub]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_name << topic_name << type_tag;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx/my_topic",
      ss << ctx->qos_library << "::" << ctx->qos_ctx_name << topic_name;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/my_topic[pub]",
      ss << ctx->qos_library << "::" << type_tag << topic_name;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/my_topic",
      ss << ctx->qos_library << "::" << topic_name;
      profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::[pub]"
      ss << ctx->qos_library << "::" << type_tag;
      profiles.push_back(ss.str());
      ss.str("");
    }

    // e.g. "/foo/bar/my_ctx::/my_topic[pub]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      topic_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::/my_topic"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" << topic_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::[pub]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar::my_ctx/my_topic[pub]"
    ss << ctx->qos_ctx_namespace << "::" << ctx->qos_ctx_name <<
      topic_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar::my_ctx/my_topic"
    ss << ctx->qos_ctx_namespace << "::" << ctx->qos_ctx_name << topic_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar::my_ctx[pub]"
    ss << ctx->qos_ctx_namespace << "::" << ctx->qos_ctx_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar::/my_topic[pub]"
    ss << ctx->qos_ctx_namespace << "::" << topic_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar::/my_topic"
    ss << ctx->qos_ctx_namespace << "::" << topic_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/::[pub]"
    ss << ctx->qos_ctx_namespace << "::" << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_topic[pub]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
      topic_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_topic"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name << topic_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_topic[pub]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << topic_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_topic"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << topic_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/my_topic[pub]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      topic_name << type_tag;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/my_topic"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << topic_name;
    profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::[pub]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << type_tag;
    profiles.push_back(ss.str());
  } catch (const std::exception & e) {
    UNUSED_ARG(e);
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t
rmw_connextdds_list_publisher_qos_profiles(
  rmw_context_impl_t * const ctx,
  const char * const topic_name,
  std::vector<std::string> & profiles)
{
  return rmw_connextdds_list_pubsub_qos_profiles(
    ctx, topic_name, RMW_CONNEXT_QOS_TAG_PUBLISHER, profiles);
}

rmw_ret_t
rmw_connextdds_list_subscription_qos_profiles(
  rmw_context_impl_t * const ctx,
  const char * const topic_name,
  std::vector<std::string> & profiles)
{
  return rmw_connextdds_list_pubsub_qos_profiles(
    ctx, topic_name, RMW_CONNEXT_QOS_TAG_SUBSCRIPTION, profiles);
}

static
rmw_ret_t
rmw_connextdds_list_clientservice_qos_profiles(
  rmw_context_impl_t * const ctx,
  const char * const service_name,
  const char * const type_tag,
  std::vector<std::string> & req_profiles,
  std::vector<std::string> & rep_profiles)
{
  const bool has_lib = ctx->qos_library.size() > 0;
  try {
    std::ostringstream ss;

    if (has_lib) {
      // e.g. "my_lib::/foo/bar/my_ctx/my_service[client][request]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
        "/" << service_name << type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
      req_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/my_ctx/my_service[client][reply]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
        "/" << service_name << type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/my_ctx/my_service[client]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
        "/" << service_name << type_tag;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/my_ctx/my_service[request]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name << "/" <<
        service_name << RMW_CONNEXT_QOS_TAG_REQUEST;
      req_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/my_ctx/my_service[reply]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name << "/" <<
        service_name << RMW_CONNEXT_QOS_TAG_REPLY;
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::/foo/bar/my_ctx/my_service",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_namespace << ctx->qos_ctx_name << "/" << service_name;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx/my_service[client][request]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_name << "/" << service_name <<
        type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
      req_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx/my_service[client][reply]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_name << "/" << service_name <<
        type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx/my_service[client]",
      ss << ctx->qos_library << "::" <<
        ctx->qos_ctx_name << "/" << service_name << type_tag;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_ctx/my_service",
      ss << ctx->qos_library << "::" << ctx->qos_ctx_name << "/" << service_name;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_service[client][request]",
      ss << ctx->qos_library << "::" <<
        service_name << type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
      req_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_service[client][reply]",
      ss << ctx->qos_library << "::" <<
        service_name << type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_service[client]",
      ss << ctx->qos_library << "::" << service_name << type_tag;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_service[request]",
      ss << ctx->qos_library << "::" <<
        service_name << RMW_CONNEXT_QOS_TAG_REQUEST;
      req_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_service[reply]",
      ss << ctx->qos_library << "::" <<
        service_name << RMW_CONNEXT_QOS_TAG_REPLY;
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::my_service",
      ss << ctx->qos_library << "::" << service_name;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::[client][request]"
      ss << ctx->qos_library << "::" << type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
      req_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::[client][reply]"
      ss << ctx->qos_library << "::" << type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
      rep_profiles.push_back(ss.str());
      ss.str("");

      // e.g. "my_lib::[client]"
      ss << ctx->qos_library << "::" << type_tag;
      req_profiles.push_back(ss.str());
      rep_profiles.push_back(ss.str());
      ss.str("");
    }

    // e.g. "/foo/bar/my_ctx::my_service[client][request]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      service_name << type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::my_service[client][reply]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      service_name << type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::my_service[client]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      service_name << type_tag;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::my_service[request]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      service_name << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::my_service[reply]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      service_name << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::my_service"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" << service_name;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::[client][request]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::[client][reply]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" <<
      type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "/foo/bar/my_ctx::[client]"
    ss << ctx->qos_ctx_namespace << ctx->qos_ctx_name << "::" << type_tag;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_service[client][request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
      "/" << service_name << type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_service[client][reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
      "/" << service_name << type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_service[client]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name <<
      "/" << service_name << type_tag;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_service[request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name << "/" <<
      service_name << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_service[reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name << "/" <<
      service_name << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::/foo/bar/my_ctx/my_service"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_namespace << ctx->qos_ctx_name << "/" << service_name;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_service[client][request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << "/" << service_name <<
      type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_service[client][reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << "/" << service_name <<
      type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_service[client]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << "/" << service_name << type_tag;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_service[request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << service_name << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_service[reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << service_name << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_ctx/my_service"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      ctx->qos_ctx_name << service_name;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_service[client][request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << service_name <<
      type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_service[client][reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << service_name <<
      type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_service[client]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << service_name << type_tag;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_service[request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      service_name << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_service[reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      service_name << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::my_service"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << service_name;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::[client][request]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      type_tag << RMW_CONNEXT_QOS_TAG_REQUEST;
    req_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::[client][reply]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" <<
      type_tag << RMW_CONNEXT_QOS_TAG_REPLY;
    rep_profiles.push_back(ss.str());
    ss.str("");

    // e.g. "ros::[client]"
    ss << RMW_CONNEXT_DEFAULT_QOS_LIBRARY << "::" << type_tag;
    req_profiles.push_back(ss.str());
    rep_profiles.push_back(ss.str());
  } catch (const std::exception & e) {
    UNUSED_ARG(e);
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}


rmw_ret_t
rmw_connextdds_list_client_qos_profiles(
  rmw_context_impl_t * const ctx,
  const char * const service_name,
  std::vector<std::string> & req_profiles,
  std::vector<std::string> & rep_profiles)
{
  return rmw_connextdds_list_clientservice_qos_profiles(
    ctx, service_name, RMW_CONNEXT_QOS_TAG_CLIENT, req_profiles, rep_profiles);
}

rmw_ret_t
rmw_connextdds_list_service_qos_profiles(
  rmw_context_impl_t * const ctx,
  const char * const service_name,
  std::vector<std::string> & req_profiles,
  std::vector<std::string> & rep_profiles)
{
  return rmw_connextdds_list_clientservice_qos_profiles(
    ctx, service_name, RMW_CONNEXT_QOS_TAG_SERVICE, req_profiles, rep_profiles);
}

/******************************************************************************
 * Node support
 ******************************************************************************/

RMW_Connext_Node *
RMW_Connext_Node::create(
  rmw_context_impl_t * const ctx)
{
  RMW_Connext_Node * node_impl =
    new (std::nothrow) RMW_Connext_Node(ctx);

  if (nullptr == node_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to allocate node implementation")
    return nullptr;
  }

  return node_impl;
}

rmw_ret_t
RMW_Connext_Node::finalize()
{
  return RMW_RET_OK;
}

/******************************************************************************
 * Publisher Implementation functions
 ******************************************************************************/

RMW_Connext_Publisher::RMW_Connext_Publisher(
  rmw_context_impl_t * const ctx,
  DDS_DataWriter * const dds_writer,
  RMW_Connext_MessageTypeSupport * const type_support,
  const bool created_topic)
: ctx(ctx),
  dds_writer(dds_writer),
  type_support(type_support),
  created_topic(created_topic),
  status_condition(dds_writer)
{
  rmw_connextdds_get_entity_gid(this->dds_writer, this->ros_gid);
}

RMW_Connext_Publisher *
RMW_Connext_Publisher::create(
  rmw_context_impl_t * const ctx,
  DDS_DomainParticipant * const dp,
  DDS_Publisher * const pub,
  const rosidl_message_type_support_t * const type_supports,
  const char * const topic_name,
  const rmw_qos_profile_t * const qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  const rmw_publisher_options_t * const publisher_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
  const bool internal,
  const RMW_Connext_MessageType msg_type,
  const void * const intro_members,
  const bool intro_members_cpp,
  std::string * const type_name)
{
  std::lock_guard<std::mutex> guard(ctx->common.node_update_mutex);
  UNUSED_ARG(internal);

  bool type_registered = false;

  RMW_Connext_MessageTypeSupport * type_support =
    RMW_Connext_MessageTypeSupport::register_type_support(
    ctx,
    type_supports,
    dp,
    type_registered,
    msg_type,
    intro_members,
    intro_members_cpp,
    type_name);

  if (nullptr == type_support) {
    RMW_CONNEXT_LOG_ERROR("failed to register type for writer")
    return nullptr;
  }

  auto scope_exit_type_unregister = rcpputils::make_scope_exit(
    [type_registered, dp, type_support, ctx]()
    {
      if (type_registered) {
        if (RMW_RET_OK !=
        RMW_Connext_MessageTypeSupport::unregister_type_support(
          ctx, dp, type_support->type_name()))
        {
          RMW_CONNEXT_LOG_ERROR(
            "failed to unregister type for writer")
        }
      }
      delete type_support;
    });

  std::string fqtopic_name;
  std::string prefix_rep(ROS_SERVICE_RESPONSE_PREFIX_STR "/");
  std::string prefix_req(ROS_SERVICE_REQUESTER_PREFIX_STR "/");
  std::string str_topic(topic_name);

  if (str_topic.find(prefix_rep) == 0 || str_topic.find(prefix_req) == 0) {
    fqtopic_name = str_topic;
  } else {
    fqtopic_name =
      rmw_connextdds_create_topic_name(
      ROS_TOPIC_PREFIX, topic_name, "", qos_policies);
  }

  DDS_Topic * topic = nullptr;
  bool topic_created = false;

  if (RMW_RET_OK !=
    ctx->assert_topic(
      dp,
      fqtopic_name.c_str(),
      type_support->type_name(),
      internal,
      &topic,
      topic_created))
  {
    RMW_CONNEXT_LOG_ERROR_A(
      "failed to assert topic: "
      "name=%s, "
      "type=%s",
      fqtopic_name.c_str(),
      type_support->type_name())
    return nullptr;
  }

  auto scope_exit_topic_delete = rcpputils::make_scope_exit(
    [topic_created, dp, topic]()
    {
      if (topic_created) {
        if (DDS_RETCODE_OK !=
        DDS_DomainParticipant_delete_topic(dp, topic))
        {
          RMW_CONNEXT_LOG_ERROR_SET(
            "failed to delete writer's topic")
        }
      }
    });

  // The following initialization generates warnings when built
  // with RTI Connext DDS Professional < 6 (e.g. 5.3.1), so use
  // DDS_DataWriterQos_initialize() for older versions.
#if !RMW_CONNEXT_DDS_API_PRO_LEGACY
  DDS_DataWriterQos dw_qos = DDS_DataWriterQos_INITIALIZER;
#else
  DDS_DataWriterQos dw_qos;
  if (DDS_RETCODE_OK != DDS_DataWriterQos_initialize(&dw_qos)) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to initialize datawriter qos")
    return nullptr;
  }
#endif /* !RMW_CONNEXT_DDS_API_PRO_LEGACY */

  DDS_DataWriterQos * const dw_qos_ptr = &dw_qos;
  auto scope_exit_dw_qos_delete =
    rcpputils::make_scope_exit(
    [dw_qos_ptr]()
    {
      if (DDS_RETCODE_OK != DDS_DataWriterQos_finalize(dw_qos_ptr)) {
        RMW_CONNEXT_LOG_ERROR_SET("failed to finalize DataWriterQoS")
      }
    });

  if (DDS_RETCODE_OK !=
    DDS_Publisher_get_default_datawriter_qos(pub, &dw_qos))
  {
    RMW_CONNEXT_LOG_ERROR_SET("failed to get default writer QoS")
    return nullptr;
  }

  DDS_DataWriter * const dds_writer =
    rmw_connextdds_create_datawriter(
    ctx,
    dp,
    pub,
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    publisher_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    internal,
    type_support,
    topic,
    &dw_qos);

  if (nullptr == dds_writer) {
    RMW_CONNEXT_LOG_ERROR("failed to create DDS writer")
    return nullptr;
  }

  auto scope_exit_dds_writer_delete =
    rcpputils::make_scope_exit(
    [pub, dds_writer]()
    {
      if (DDS_RETCODE_OK !=
      DDS_Publisher_delete_datawriter(pub, dds_writer))
      {
        RMW_CONNEXT_LOG_ERROR_SET(
          "failed to delete DDS DataWriter")
      }
    });

  RMW_Connext_Publisher * rmw_pub_impl =
    new (std::nothrow) RMW_Connext_Publisher(
    ctx, dds_writer, type_support, topic_created);

  if (nullptr == rmw_pub_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to allocate RMW publisher")
    return nullptr;
  }

  scope_exit_type_unregister.cancel();
  scope_exit_topic_delete.cancel();
  scope_exit_dds_writer_delete.cancel();

  return rmw_pub_impl;
}

rmw_ret_t
RMW_Connext_Publisher::finalize()
{
  std::lock_guard<std::mutex> guard(ctx->common.node_update_mutex);

  RMW_CONNEXT_LOG_DEBUG_A(
    "finalizing publisher: pub=%p, type=%s",
    (void *)this, this->type_support->type_name())

  // Make sure publisher's condition is detached from any waitset
  this->status_condition.invalidate();

  if (DDS_RETCODE_OK !=
    DDS_Publisher_delete_datawriter(
      this->dds_publisher(), this->dds_writer))
  {
    RMW_CONNEXT_LOG_ERROR_SET("failed to delete DDS DataWriter")
    return RMW_RET_ERROR;
  }

  DDS_DomainParticipant * const participant = this->dds_participant();

  if (this->created_topic) {
    DDS_Topic * const topic = this->dds_topic();

    RMW_CONNEXT_LOG_DEBUG_A(
      "deleting topic: name=%s",
      DDS_TopicDescription_get_name(
        DDS_Topic_as_topicdescription(topic)))

    DDS_ReturnCode_t rc =
      DDS_DomainParticipant_delete_topic(participant, topic);

    if (DDS_RETCODE_OK != rc) {
      RMW_CONNEXT_LOG_ERROR_SET("failed to delete DDS Topic")
      return RMW_RET_ERROR;
    }
  }

  rmw_ret_t rc = RMW_Connext_MessageTypeSupport::unregister_type_support(
    this->ctx, participant, this->type_support->type_name());

  if (RMW_RET_OK != rc) {
    return rc;
  }

  delete this->type_support;
  this->type_support = nullptr;

  return RMW_RET_OK;
}

#ifndef DDS_GUID_INITIALIZER
#define DDS_GUID_INITIALIZER        DDS_GUID_DEFAULT
#endif /* DDS_GUID_INITIALIZER */

rmw_ret_t
RMW_Connext_Publisher::requestreply_header_to_dds(
  const RMW_Connext_RequestReplyMessage * const rr_msg,
  DDS_SampleIdentity_t * const sample_identity,
  DDS_SampleIdentity_t * const related_sample_identity)
{
  struct DDS_GUID_t src_guid = DDS_GUID_INITIALIZER;
  struct DDS_SequenceNumber_t src_sn = DDS_SEQUENCE_NUMBER_UNKNOWN;

  rmw_ret_t rc = RMW_RET_ERROR;
  rc = rmw_connextdds_gid_to_guid(rr_msg->gid, src_guid);
  if (RMW_RET_OK != rc) {
    return rc;
  }

  rmw_connextdds_sn_ros_to_dds(rr_msg->sn, src_sn);

  if (rr_msg->request) {
    sample_identity->writer_guid = src_guid;
    sample_identity->sequence_number = src_sn;
  } else {
    related_sample_identity->writer_guid = src_guid;
    related_sample_identity->sequence_number = src_sn;
  }

  return RMW_RET_OK;
}


rmw_ret_t
RMW_Connext_Publisher::write(
  const void * const ros_message,
  const bool serialized,
  int64_t * const sn_out)
{
  RMW_Connext_Message user_msg;
  user_msg.user_data = ros_message;
  user_msg.serialized = serialized;
  user_msg.type_support = this->type_support;

  return rmw_connextdds_write_message(this, &user_msg, sn_out);
}


size_t
RMW_Connext_Publisher::subscriptions_count()
{
  DDS_PublicationMatchedStatus status =
    DDS_PublicationMatchedStatus_INITIALIZER;

  if (DDS_RETCODE_OK !=
    DDS_DataWriter_get_publication_matched_status(
      this->dds_writer, &status))
  {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to get publication matched status")
    return 0;
  }

  return status.current_count;
}

rmw_ret_t
RMW_Connext_Publisher::assert_liveliness()
{
  if (DDS_RETCODE_OK !=
    DDS_DataWriter_assert_liveliness(this->dds_writer))
  {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to assert writer liveliness")
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_Publisher::qos(rmw_qos_profile_t * const qos)
{
  // The following initialization generates warnings when built
  // with RTI Connext DDS Professional < 6 (e.g. 5.3.1), so use
  // DDS_DataWriterQos_initialize() for older versions.
#if !RMW_CONNEXT_DDS_API_PRO_LEGACY
  DDS_DataWriterQos dw_qos = DDS_DataWriterQos_INITIALIZER;
#else
  DDS_DataWriterQos dw_qos;
  if (DDS_RETCODE_OK != DDS_DataWriterQos_initialize(&dw_qos)) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to initialize datawriter qos")
    return RMW_RET_ERROR;
  }
#endif /* !RMW_CONNEXT_DDS_API_PRO_LEGACY */

  if (DDS_RETCODE_OK != DDS_DataWriter_get_qos(this->dds_writer, &dw_qos)) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to get DDS writer's qos")
    return RMW_RET_ERROR;
  }

  rmw_ret_t rc = rmw_connextdds_datawriter_qos_to_ros(&dw_qos, qos);

  DDS_DataWriterQos_finalize(&dw_qos);
  return rc;
}

rmw_publisher_t *
rmw_connextdds_create_publisher(
  rmw_context_impl_t * const ctx,
  const rmw_node_t * const node,
  DDS_DomainParticipant * const dp,
  DDS_Publisher * const pub,
  const rosidl_message_type_support_t * const type_supports,
  const char * const topic_name,
  const rmw_qos_profile_t * const qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  const rmw_publisher_options_t * const publisher_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
  const bool internal)
{
  RMW_Connext_Publisher * rmw_pub_impl =
    RMW_Connext_Publisher::create(
    ctx,
    dp,
    pub,
    type_supports,
    topic_name,
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    publisher_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    internal);

  if (nullptr == rmw_pub_impl) {
    RMW_CONNEXT_LOG_ERROR(
      "failed to allocate RMW_Connext_Publisher")
    return nullptr;
  }

  auto scope_exit_rmw_writer_impl_delete =
    rcpputils::make_scope_exit(
    [rmw_pub_impl]()
    {
      if (RMW_RET_OK != rmw_pub_impl->finalize()) {
        RMW_CONNEXT_LOG_ERROR(
          "failed to finalize RMW_Connext_Publisher")
      }
      delete rmw_pub_impl;
    });

  rmw_publisher_t * rmw_publisher = rmw_publisher_allocate();
  if (nullptr == rmw_publisher) {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to allocate RMW publisher")
    return nullptr;
  }
  rmw_publisher->topic_name = nullptr;

  auto scope_exit_rmw_writer_delete = rcpputils::make_scope_exit(
    [rmw_publisher]() {
      if (nullptr != rmw_publisher->topic_name) {
        rmw_free(const_cast<char *>(rmw_publisher->topic_name));
      }
      rmw_publisher_free(rmw_publisher);
    });

  size_t topic_name_len = strlen(topic_name);

  rmw_publisher->implementation_identifier = RMW_CONNEXTDDS_ID;
  rmw_publisher->data = rmw_pub_impl;
  rmw_publisher->topic_name =
    reinterpret_cast<char *>(rmw_allocate(topic_name_len + 1));
  if (nullptr == rmw_publisher->topic_name) {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to allocate publisher's topic name")
    return nullptr;
  }
  memcpy(
    const_cast<char *>(rmw_publisher->topic_name),
    topic_name,
    topic_name_len + 1);
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  rmw_publisher->options = *publisher_options;
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
#if RMW_CONNEXT_HAVE_LOAN_MESSAGE
  rmw_publisher->can_loan_messages = false;
#endif /* RMW_CONNEXT_HAVE_LOAN_MESSAGE */

  if (!internal) {
    if (RMW_RET_OK != rmw_pub_impl->enable()) {
      RMW_CONNEXT_LOG_ERROR("failed to enable publisher")
      return nullptr;
    }

    if (RMW_RET_OK !=
      rmw_connextdds_graph_on_publisher_created(
        ctx, node, rmw_pub_impl))
    {
      RMW_CONNEXT_LOG_ERROR("failed to update graph for publisher")
      return nullptr;
    }
  }


  scope_exit_rmw_writer_impl_delete.cancel();
  scope_exit_rmw_writer_delete.cancel();
  return rmw_publisher;
}

rmw_ret_t
rmw_connextdds_destroy_publisher(
  rmw_context_impl_t * const ctx,
  rmw_publisher_t * const rmw_publisher)
{
  UNUSED_ARG(ctx);

  RMW_Connext_Publisher * const rmw_pub_impl =
    static_cast<RMW_Connext_Publisher *>(rmw_publisher->data);

  if (nullptr == rmw_pub_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("invalid publisher data")
    return RMW_RET_ERROR;
  }

  rmw_ret_t rc = rmw_pub_impl->finalize();
  if (RMW_RET_OK != rc) {
    return rc;
  }

  delete rmw_pub_impl;
  rmw_free(const_cast<char *>(rmw_publisher->topic_name));
  rmw_publisher_free(rmw_publisher);

  return RMW_RET_OK;
}


/******************************************************************************
 * Subscriber Implementation functions
 ******************************************************************************/

RMW_Connext_Subscriber::RMW_Connext_Subscriber(
  rmw_context_impl_t * const ctx,
  DDS_DataReader * const dds_reader,
  DDS_Topic * const dds_topic,
  RMW_Connext_MessageTypeSupport * const type_support,
  const bool ignore_local,
  const bool created_topic,
  DDS_TopicDescription * const dds_topic_cft,
  const bool internal)
: internal(internal),
  ctx(ctx),
  dds_reader(dds_reader),
  dds_topic(dds_topic),
  dds_topic_cft(dds_topic_cft),
  type_support(type_support),
  created_topic(created_topic),
  status_condition(dds_reader, ignore_local)
{
  rmw_connextdds_get_entity_gid(this->dds_reader, this->ros_gid);

  RMW_Connext_UntypedSampleSeq def_data_seq =
    RMW_Connext_UntypedSampleSeq_INITIALIZER;
  DDS_SampleInfoSeq def_info_seq = DDS_SEQUENCE_INITIALIZER;
  this->loan_data = def_data_seq;
  this->loan_info = def_info_seq;
  this->loan_len = 0;
  this->loan_next = 0;
}

RMW_Connext_Subscriber *
RMW_Connext_Subscriber::create(
  rmw_context_impl_t * const ctx,
  DDS_DomainParticipant * const dp,
  DDS_Subscriber * const sub,
  const rosidl_message_type_support_t * const type_supports,
  const char * const topic_name,
  const rmw_qos_profile_t * const qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  const rmw_subscription_options_t * const subscriber_options,
#else
  const bool ignore_local_publications,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
  const bool internal,
  const RMW_Connext_MessageType msg_type,
  const void * const intro_members,
  const bool intro_members_cpp,
  std::string * const type_name,
  const char * const cft_name,
  const char * const cft_filter)
{
  std::lock_guard<std::mutex> guard(ctx->common.node_update_mutex);

  bool type_registered = false;

  RMW_Connext_MessageTypeSupport * const type_support =
    RMW_Connext_MessageTypeSupport::register_type_support(
    ctx,
    type_supports,
    dp,
    type_registered,
    msg_type,
    intro_members,
    intro_members_cpp,
    type_name);

  if (nullptr == type_support) {
    RMW_CONNEXT_LOG_ERROR("failed to register type for reader")
    return nullptr;
  }

  auto scope_exit_type_unregister = rcpputils::make_scope_exit(
    [type_registered, dp, type_support, ctx]()
    {
      if (type_registered) {
        if (RMW_RET_OK !=
        RMW_Connext_MessageTypeSupport::unregister_type_support(
          ctx, dp, type_support->type_name()))
        {
          RMW_CONNEXT_LOG_ERROR(
            "failed to unregister type for writer")
        }
      }
    });

  std::string fqtopic_name;
  std::string prefix_rep(ROS_SERVICE_RESPONSE_PREFIX_STR "/");
  std::string prefix_req(ROS_SERVICE_REQUESTER_PREFIX_STR "/");
  std::string str_topic(topic_name);

  if (str_topic.find(prefix_rep) == 0 || str_topic.find(prefix_req) == 0) {
    fqtopic_name = str_topic;
  } else {
    fqtopic_name =
      rmw_connextdds_create_topic_name(
      ROS_TOPIC_PREFIX, topic_name, "", qos_policies);
  }

  DDS_Topic * topic = nullptr;
  DDS_TopicDescription * cft_topic = nullptr;
  bool topic_created = false;

  if (RMW_RET_OK !=
    ctx->assert_topic(
      dp,
      fqtopic_name.c_str(),
      type_support->type_name(),
      internal,
      &topic,
      topic_created))
  {
    RMW_CONNEXT_LOG_ERROR_A(
      "failed to assert topic: "
      "name=%s, "
      "type=%s",
      fqtopic_name.c_str(),
      type_support->type_name())
    return nullptr;
  }

  auto scope_exit_topic_delete = rcpputils::make_scope_exit(
    [ctx, topic_created, dp, topic, cft_topic]()
    {
      if (nullptr != cft_topic) {
        if (RMW_RET_OK !=
        rmw_connextdds_delete_contentfilteredtopic(ctx, dp, cft_topic))
        {
          RMW_CONNEXT_LOG_ERROR("failed to delete content-filtered topic")
        }
      }
      if (topic_created) {
        if (DDS_RETCODE_OK !=
        DDS_DomainParticipant_delete_topic(dp, topic))
        {
          RMW_CONNEXT_LOG_ERROR_SET(
            "failed to delete reader's topic")
        }
      }
    });

  DDS_TopicDescription * sub_topic = DDS_Topic_as_topicdescription(topic);

  if (nullptr != cft_name) {
    rmw_ret_t cft_rc =
      rmw_connextdds_create_contentfilteredtopic(
      ctx, dp, topic, cft_name, cft_filter, &cft_topic);

    if (RMW_RET_OK != cft_rc) {
      if (RMW_RET_UNSUPPORTED != cft_rc) {
        return nullptr;
      }
    } else {
      sub_topic = cft_topic;
    }
  }

  // The following initialization generates warnings when built
  // with RTI Connext DDS Professional < 6 (e.g. 5.3.1), so use
  // DDS_DataWriterQos_initialize() for older versions.
#if !RMW_CONNEXT_DDS_API_PRO_LEGACY
  DDS_DataReaderQos dr_qos = DDS_DataReaderQos_INITIALIZER;
#else
  DDS_DataReaderQos dr_qos;
  if (DDS_RETCODE_OK != DDS_DataReaderQos_initialize(&dr_qos)) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to initialize datareader qos")
    return nullptr;
  }
#endif /* !RMW_CONNEXT_DDS_API_PRO_LEGACY */

  DDS_DataReaderQos * const dr_qos_ptr = &dr_qos;
  auto scope_exit_dr_qos_delete =
    rcpputils::make_scope_exit(
    [dr_qos_ptr]()
    {
      DDS_DataReaderQos_finalize(dr_qos_ptr);
    });

  if (DDS_RETCODE_OK !=
    DDS_Subscriber_get_default_datareader_qos(sub, &dr_qos))
  {
    RMW_CONNEXT_LOG_ERROR_SET("failed to get default reader QoS")
    return nullptr;
  }

  DDS_DataReader * dds_reader =
    rmw_connextdds_create_datareader(
    ctx,
    dp,
    sub,
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    subscriber_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    internal,
    type_support,
    sub_topic,
    &dr_qos);

  if (nullptr == dds_reader) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to create DDS reader")
    return nullptr;
  }

  auto scope_exit_dds_reader_delete =
    rcpputils::make_scope_exit(
    [sub, dds_reader]()
    {
      if (DDS_RETCODE_OK !=
      DDS_Subscriber_delete_datareader(sub, dds_reader))
      {
        RMW_CONNEXT_LOG_ERROR_SET(
          "failed to delete DDS DataWriter")
      }
    });

  RMW_Connext_Subscriber * rmw_sub_impl =
    new (std::nothrow) RMW_Connext_Subscriber(
    ctx,
    dds_reader,
    topic,
    type_support,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    subscriber_options->ignore_local_publications,
#else
    ignore_local_publications,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    topic_created,
    cft_topic,
    internal);

  if (nullptr == rmw_sub_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to allocate RMW subscriber")
    return nullptr;
  }
  scope_exit_type_unregister.cancel();
  scope_exit_topic_delete.cancel();
  scope_exit_dds_reader_delete.cancel();

  return rmw_sub_impl;
}

rmw_ret_t
RMW_Connext_Subscriber::finalize()
{
  std::lock_guard<std::mutex> guard(ctx->common.node_update_mutex);

  RMW_CONNEXT_LOG_DEBUG_A(
    "finalizing subscriber: sub=%p, type=%s",
    (void *)this, this->type_support->type_name())

  // Make sure subscriber's condition is detached from any waitset
  this->status_condition.invalidate();

  if (this->loan_len > 0) {
    this->loan_next = this->loan_len;
    if (RMW_RET_OK != this->return_messages()) {
      return RMW_RET_ERROR;
    }
  }

  if (DDS_RETCODE_OK !=
    DDS_Subscriber_delete_datareader(
      this->dds_subscriber(), this->dds_reader))
  {
    RMW_CONNEXT_LOG_ERROR_SET("failed to delete DDS DataReader")
    return RMW_RET_ERROR;
  }

  DDS_DomainParticipant * const participant = this->dds_participant();

  if (nullptr != this->dds_topic_cft) {
    rmw_ret_t cft_rc = rmw_connextdds_delete_contentfilteredtopic(
      ctx, participant, this->dds_topic_cft);

    if (RMW_RET_OK != cft_rc) {
      return cft_rc;
    }
  }

  if (this->created_topic) {
    DDS_Topic * const topic = this->dds_topic;

    RMW_CONNEXT_LOG_DEBUG_A(
      "deleting topic: name=%s",
      DDS_TopicDescription_get_name(
        DDS_Topic_as_topicdescription(topic)))

    DDS_ReturnCode_t rc =
      DDS_DomainParticipant_delete_topic(participant, topic);

    if (DDS_RETCODE_OK != rc) {
      RMW_CONNEXT_LOG_ERROR_SET("failed to delete DDS Topic")
      return RMW_RET_ERROR;
    }
  }

  rmw_ret_t rc = RMW_Connext_MessageTypeSupport::unregister_type_support(
    this->ctx, participant, this->type_support->type_name());

  if (RMW_RET_OK != rc) {
    return rc;
  }

  delete this->type_support;
  this->type_support = nullptr;

  return RMW_RET_OK;
}

size_t
RMW_Connext_Subscriber::publications_count()
{
  DDS_SubscriptionMatchedStatus status =
    DDS_SubscriptionMatchedStatus_INITIALIZER;

  if (DDS_RETCODE_OK !=
    DDS_DataReader_get_subscription_matched_status(
      this->dds_reader, &status))
  {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to get subscription matched status")
    return 0;
  }

  return status.current_count;
}


rmw_ret_t
RMW_Connext_Subscriber::qos(rmw_qos_profile_t * const qos)
{
  // The following initialization generates warnings when built
  // with RTI Connext DDS Professional < 6 (e.g. 5.3.1), so use
  // DDS_DataWriterQos_initialize() for older versions.
#if !RMW_CONNEXT_DDS_API_PRO_LEGACY
  DDS_DataReaderQos dr_qos = DDS_DataReaderQos_INITIALIZER;
#else
  DDS_DataReaderQos dr_qos;
  if (DDS_RETCODE_OK != DDS_DataReaderQos_initialize(&dr_qos)) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to initialize datareader qos")
    return RMW_RET_ERROR;
  }
#endif /* !RMW_CONNEXT_DDS_API_PRO_LEGACY */

  if (DDS_RETCODE_OK != DDS_DataReader_get_qos(this->dds_reader, &dr_qos)) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to get DDS reader's qos")
    return RMW_RET_ERROR;
  }

  rmw_ret_t rc = rmw_connextdds_datareader_qos_to_ros(&dr_qos, qos);

  DDS_DataReaderQos_finalize(&dr_qos);
  return rc;
}

rmw_ret_t
RMW_Connext_Subscriber::take_message(
  void * const ros_message,
  rmw_message_info_t * const message_info,
  bool * const taken,
  const DDS_InstanceHandle_t * const request_writer_handle)
{
  *taken = false;
  size_t taken_count = 0;
  void * ros_messages[1];
  ros_messages[0] = ros_message;
  rmw_ret_t rc = this->take_next(
    ros_messages,
    message_info,
    1,
    &taken_count,
    false /* serialized*/,
    request_writer_handle);
  if (RMW_RET_OK == rc) {
    *taken = taken_count > 0;
  }
  return rc;
}

#if RMW_CONNEXT_HAVE_TAKE_SEQ
rmw_ret_t
RMW_Connext_Subscriber::take(
  rmw_message_sequence_t * const message_sequence,
  rmw_message_info_sequence_t * const message_info_sequence,
  const size_t max_samples,
  size_t * const taken)
{
  if (max_samples == 0 ||
    message_sequence->capacity < max_samples ||
    message_info_sequence->capacity != message_sequence->capacity)
  {
    return RMW_RET_INVALID_ARGUMENT;
  }
  return this->take_next(
    message_sequence->data,
    message_info_sequence->data,
    max_samples,
    taken,
    false /* serialized*/);
}
#endif /* RMW_CONNEXT_HAVE_TAKE_SEQ */

rmw_ret_t
RMW_Connext_Subscriber::take_serialized(
  rmw_serialized_message_t * const serialized_message,
  rmw_message_info_t * const message_info,
  bool * const taken)
{
  *taken = false;
  size_t taken_count = 0;
  void * ros_messages[1];
  ros_messages[0] = serialized_message;
  rmw_ret_t rc = this->take_next(
    ros_messages,
    message_info,
    1,
    &taken_count,
    true /* serialized*/);
  if (RMW_RET_OK == rc) {
    *taken = taken_count > 0;
  }
  return rc;
}

rmw_ret_t
RMW_Connext_Subscriber::loan_messages()
{
  /* this function should only be called once all previously
     loaned messages have been returned */
  RMW_CONNEXT_ASSERT(this->loan_len == 0)
  RMW_CONNEXT_ASSERT(this->loan_next == 0)

  if (RMW_RET_OK != rmw_connextdds_take_samples(this)) {
    return RMW_RET_ERROR;
  }

  this->loan_len = DDS_UntypedSampleSeq_get_length(&this->loan_data);

  RMW_CONNEXT_LOG_DEBUG_A(
    "[%s] loaned messages: %lu",
    this->type_support->type_name(), this->loan_len)

  return this->status_condition.set_data_available(this->loan_len > 0);
}

rmw_ret_t
RMW_Connext_Subscriber::return_messages()
{
  /* this function should be called only if a loan is available */
  RMW_CONNEXT_ASSERT(this->loan_len > 0)

  RMW_CONNEXT_LOG_DEBUG_A(
    "[%s] return loaned messages: %lu",
    this->type_support->type_name(), this->loan_len)

  this->loan_len = 0;
  this->loan_next = 0;

  rmw_ret_t rc_result = RMW_RET_OK;
  rmw_ret_t rc = rmw_connextdds_return_samples(this);
  if (RMW_RET_OK != rc) {
    rc_result = rc;
  }

  rc = this->status_condition.set_data_available(false);
  if (RMW_RET_OK != rc) {
    rc_result = rc;
  }

  return rc_result;
}

void
RMW_Connext_Subscriber::requestreply_header_from_dds(
  RMW_Connext_RequestReplyMessage * const rr_msg,
  const DDS_SampleIdentity_t * const sample_identity,
  const DDS_SampleIdentity_t * const related_sample_identity)
{
  const struct DDS_GUID_t * src_guid = nullptr;
  const struct DDS_SequenceNumber_t * src_sn = nullptr;

  if (rr_msg->request) {
    src_guid = &sample_identity->writer_guid;
    src_sn = &sample_identity->sequence_number;
  } else {
    src_guid = &related_sample_identity->writer_guid;
    src_sn = &related_sample_identity->sequence_number;
  }

  rmw_connextdds_guid_to_gid(*src_guid, rr_msg->gid);
  rmw_connextdds_sn_dds_to_ros(*src_sn, rr_msg->sn);
}

rmw_ret_t
RMW_Connext_Subscriber::take_next(
  void ** const ros_messages,
  rmw_message_info_t * const message_infos,
  const size_t max_samples,
  size_t * const taken,
  const bool serialized,
  const DDS_InstanceHandle_t * const request_writer_handle)
{
  rmw_ret_t rc = RMW_RET_OK;

  *taken = 0;

  std::lock_guard<std::mutex> lock(this->loan_mutex);

  while (*taken < max_samples) {
    rc = this->loan_messages_if_needed();
    if (RMW_RET_OK != rc) {
      return rc;
    }

    if (this->loan_len == 0) {
      /* no data available on reader */
      return RMW_RET_OK;
    }

    for (; *taken < max_samples && this->loan_next < this->loan_len;
      this->loan_next++)
    {
      rcutils_uint8_array_t * data_buffer =
        reinterpret_cast<rcutils_uint8_array_t *>(
        DDS_UntypedSampleSeq_get_reference(
          &this->loan_data, static_cast<DDS_Long>(this->loan_next)));
      DDS_SampleInfo * info =
        DDS_SampleInfoSeq_get_reference(
        &this->loan_info, static_cast<DDS_Long>(this->loan_next));

      if (info->valid_data) {
        bool accepted = false;
        if (RMW_RET_OK != rmw_connextdds_filter_sample(
            this, data_buffer, info, request_writer_handle, &accepted))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to filter received sample")
          return RMW_RET_ERROR;
        }
        if (!accepted) {
          RMW_CONNEXT_LOG_DEBUG_A(
            "[%s] DROPPED message",
            this->type_support->type_name())
          continue;
        }

        void * ros_message = ros_messages[*taken];

        if (serialized) {
          if (RCUTILS_RET_OK !=
            rcutils_uint8_array_copy(
              reinterpret_cast<rcutils_uint8_array_t *>(ros_message),
              data_buffer))
          {
            RMW_CONNEXT_LOG_ERROR_SET("failed to copy uint8 array")
            return RMW_RET_ERROR;
          }
        } else {
#if !RMW_CONNEXT_EMULATE_REQUESTREPLY
          if (this->type_support->type_requestreply()) {
            RMW_Connext_RequestReplyMessage * const rr_msg =
              reinterpret_cast<RMW_Connext_RequestReplyMessage *>(ros_message);

            DDS_SampleIdentity_t identity,
              related_sample_identity;

            DDS_SampleInfo_get_sample_identity(
              info, &identity);
            DDS_SampleInfo_get_related_sample_identity(
              info, &related_sample_identity);

            this->requestreply_header_from_dds(
              rr_msg, &identity, &related_sample_identity);
          }
#endif /* RMW_CONNEXT_EMULATE_REQUESTREPLY */
          size_t deserialized_size = 0;

          if (RMW_RET_OK !=
            this->type_support->deserialize(
              ros_message, data_buffer, deserialized_size))
          {
            RMW_CONNEXT_LOG_ERROR_SET(
              "failed to deserialize taken sample")
            return RMW_RET_ERROR;
          }
        }

        if (nullptr != message_infos) {
          rmw_message_info_t * message_info = &message_infos[*taken];
          rmw_connextdds_message_info_from_dds(message_info, info);
        }

        *taken += 1;
        continue;
      }
    }
  }
  RMW_CONNEXT_LOG_DEBUG_A(
    "[%s] taken messages: %lu",
    this->type_support->type_name(), *taken)

  if (this->loan_len && this->loan_next >= this->loan_len) {
    rc = this->return_messages();
  }

  return rc;
}

rmw_subscription_t *
rmw_connextdds_create_subscriber(
  rmw_context_impl_t * const ctx,
  const rmw_node_t * const node,
  DDS_DomainParticipant * const dp,
  DDS_Subscriber * const sub,
  const rosidl_message_type_support_t * const type_supports,
  const char * const topic_name,
  const rmw_qos_profile_t * const qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  const rmw_subscription_options_t * const subscriber_options,
#else
  const bool ignore_local_publications,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
  const bool internal)
{
  UNUSED_ARG(internal);

  RMW_Connext_Subscriber * rmw_sub_impl =
    RMW_Connext_Subscriber::create(
    ctx,
    dp,
    sub,
    type_supports,
    topic_name,
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    subscriber_options,
#else
    ignore_local_publications,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    internal);

  if (nullptr == rmw_sub_impl) {
    RMW_CONNEXT_LOG_ERROR(
      "failed to allocate RMW_Connext_Subscriber")
    return nullptr;
  }

  auto scope_exit_rmw_reader_impl_delete =
    rcpputils::make_scope_exit(
    [rmw_sub_impl]()
    {
      if (RMW_RET_OK != rmw_sub_impl->finalize()) {
        RMW_CONNEXT_LOG_ERROR(
          "failed to finalize RMW_Connext_Subscriber")
      }
      delete rmw_sub_impl;
    });

  rmw_subscription_t * rmw_subscriber = rmw_subscription_allocate();
  if (nullptr == rmw_subscriber) {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to allocate RMW subscriber")
    return nullptr;
  }

  auto scope_exit_rmw_reader_delete = rcpputils::make_scope_exit(
    [rmw_subscriber]() {
      if (nullptr != rmw_subscriber->topic_name) {
        rmw_free(const_cast<char *>(rmw_subscriber->topic_name));
      }
      rmw_subscription_free(rmw_subscriber);
    });

  size_t topic_name_len = strlen(topic_name);

  rmw_subscriber->implementation_identifier = RMW_CONNEXTDDS_ID;
  rmw_subscriber->data = rmw_sub_impl;
  rmw_subscriber->topic_name =
    reinterpret_cast<char *>(rmw_allocate(topic_name_len + 1));
  if (nullptr == rmw_subscriber->topic_name) {
    RMW_CONNEXT_LOG_ERROR_SET(
      "failed to allocate subscriber's topic name")
    return nullptr;
  }
  memcpy(
    const_cast<char *>(rmw_subscriber->topic_name),
    topic_name,
    topic_name_len + 1);
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  rmw_subscriber->options = *subscriber_options;
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
#if RMW_CONNEXT_HAVE_LOAN_MESSAGE
  rmw_subscriber->can_loan_messages = false;
#endif /* RMW_CONNEXT_HAVE_LOAN_MESSAGE */

  if (!internal) {
    if (RMW_RET_OK != rmw_sub_impl->enable()) {
      RMW_CONNEXT_LOG_ERROR("failed to enable subscription")
      return nullptr;
    }

    if (RMW_RET_OK !=
      rmw_connextdds_graph_on_subscriber_created(
        ctx, node, rmw_sub_impl))
    {
      RMW_CONNEXT_LOG_ERROR("failed to update graph for subscriber")
      return nullptr;
    }
  }

  scope_exit_rmw_reader_impl_delete.cancel();
  scope_exit_rmw_reader_delete.cancel();
  return rmw_subscriber;
}

rmw_ret_t
rmw_connextdds_destroy_subscriber(
  rmw_context_impl_t * const ctx,
  rmw_subscription_t * const rmw_subscriber)
{
  UNUSED_ARG(ctx);

  RMW_Connext_Subscriber * const rmw_sub_impl =
    static_cast<RMW_Connext_Subscriber *>(rmw_subscriber->data);

  if (nullptr == rmw_sub_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("invalid subscriber data")
    return RMW_RET_ERROR;
  }

  rmw_ret_t rc = rmw_sub_impl->finalize();
  if (RMW_RET_OK != rc) {
    return rc;
  }

  delete rmw_sub_impl;
  rmw_free(const_cast<char *>(rmw_subscriber->topic_name));
  rmw_subscription_free(rmw_subscriber);

  return RMW_RET_OK;
}

static
constexpr uint64_t C_NANOSECONDS_PER_SEC = 1000000000ULL;

#define dds_time_to_u64(t_) \
  ((C_NANOSECONDS_PER_SEC * (uint64_t)(t_)->sec) + (uint64_t)(t_)->nanosec)

void
rmw_connextdds_message_info_from_dds(
  rmw_message_info_t * const to,
  const DDS_SampleInfo * const from)
{
  rmw_connextdds_ih_to_gid(from->publication_handle, to->publisher_gid);
// Message timestamps are disabled on Windows because RTI Connext DDS
// does not support a high enough clock resolution by default (see: _ftime()).
#if RMW_CONNEXT_HAVE_MESSAGE_INFO_TS && !RTI_WIN32
  to->source_timestamp = dds_time_to_u64(&from->source_timestamp);
  to->received_timestamp = dds_time_to_u64(&from->reception_timestamp);
#endif /* RMW_CONNEXT_HAVE_MESSAGE_INFO_TS */
}

/******************************************************************************
 * Guard Condition Implementation functions
 ******************************************************************************/

rmw_guard_condition_t *
rmw_connextdds_create_guard_condition()
{
  RMW_Connext_GuardCondition * const gcond =
    new (std::nothrow) RMW_Connext_GuardCondition();

  if (nullptr == gcond) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to create guard condition")
    return nullptr;
  }

  rmw_guard_condition_t * gcond_handle = rmw_guard_condition_allocate();
  if (nullptr == gcond_handle) {
    delete gcond;
    RMW_CONNEXT_LOG_ERROR_SET("failed to create guard condition handle")
    return nullptr;
  }

  gcond_handle->implementation_identifier = RMW_CONNEXTDDS_ID;
  gcond_handle->data = gcond;
  return gcond_handle;
}

rmw_ret_t
rmw_connextdds_destroy_guard_condition(
  rmw_guard_condition_t * const gcond_handle)
{
  RMW_Connext_GuardCondition * const gcond =
    reinterpret_cast<RMW_Connext_GuardCondition *>(gcond_handle->data);

  delete gcond;

  rmw_guard_condition_free(gcond_handle);

  return RMW_RET_OK;
}

rmw_ret_t
rmw_connextdds_trigger_guard_condition(
  const rmw_guard_condition_t * const gcond_handle)
{
  RMW_Connext_GuardCondition * const gcond =
    reinterpret_cast<RMW_Connext_GuardCondition *>(gcond_handle->data);

  return gcond->trigger();
}

rmw_wait_set_t *
rmw_connextdds_create_waitset(const size_t max_conditions)
{
  UNUSED_ARG(max_conditions);

  rmw_wait_set_t * const rmw_ws = rmw_wait_set_allocate();
  if (nullptr == rmw_ws) {
    RMW_CONNEXT_LOG_ERROR("failed to allocate RMW WaitSet")
    return nullptr;
  }
  auto scope_exit_ws_delete = rcpputils::make_scope_exit(
    [rmw_ws]()
    {
      rmw_wait_set_free(rmw_ws);
    });

  RMW_Connext_WaitSet * const ws_impl =
    new (std::nothrow) RMW_Connext_WaitSet();


  if (nullptr == ws_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to allocate WaitSet implementation")
    return nullptr;
  }

  rmw_ws->implementation_identifier = RMW_CONNEXTDDS_ID;
  rmw_ws->data = ws_impl;

  scope_exit_ws_delete.cancel();
  return rmw_ws;
}

rmw_ret_t
rmw_connextdds_destroy_waitset(rmw_wait_set_t * const rmw_ws)
{
  RMW_Connext_WaitSet * const ws_impl =
    reinterpret_cast<RMW_Connext_WaitSet *>(rmw_ws->data);

  delete ws_impl;

  rmw_wait_set_free(rmw_ws);

  return RMW_RET_OK;
}

rmw_ret_t
rmw_connextdds_waitset_wait(
  rmw_wait_set_t * const rmw_ws,
  rmw_subscriptions_t * subs,
  rmw_guard_conditions_t * gcs,
  rmw_services_t * srvs,
  rmw_clients_t * cls,
  rmw_events_t * evs,
  const rmw_time_t * wait_timeout)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_ws, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    rmw_ws,
    rmw_ws->implementation_identifier,
    RMW_CONNEXTDDS_ID,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);

  RMW_Connext_WaitSet * const ws_impl =
    reinterpret_cast<RMW_Connext_WaitSet *>(rmw_ws->data);

  return ws_impl->wait(subs, gcs, srvs, cls, evs, wait_timeout);
}

/******************************************************************************
 * GUID functions
 ******************************************************************************/
rmw_ret_t
rmw_connextdds_gid_to_guid(const rmw_gid_t & gid, struct DDS_GUID_t & guid)
{
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    &gid,
    gid.implementation_identifier,
    RMW_CONNEXTDDS_ID,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);

  static_assert(
    RMW_GID_STORAGE_SIZE >= sizeof(guid.value),
    "rmw_gid_t type too small for an DDS GUID");

  memcpy(guid.value, gid.data, sizeof(guid.value));

  return RMW_RET_OK;
}

rmw_ret_t
rmw_connextdds_guid_to_gid(const struct DDS_GUID_t & guid, rmw_gid_t & gid)
{
  static_assert(
    RMW_GID_STORAGE_SIZE >= sizeof(guid.value),
    "rmw_gid_t type too small for an DDS GUID");
  memset(&gid, 0, sizeof(gid));
  memcpy(gid.data, guid.value, sizeof(guid.value));
  gid.implementation_identifier = RMW_CONNEXTDDS_ID;

  return RMW_RET_OK;
}

void rmw_connextdds_get_entity_gid(
  DDS_Entity * const entity,
  rmw_gid_t & gid)
{
  DDS_InstanceHandle_t ih = DDS_Entity_get_instance_handle(entity);
  rmw_connextdds_ih_to_gid(ih, gid);
}

void rmw_connextdds_get_entity_gid(
  DDS_DomainParticipant * const dp, rmw_gid_t & gid)
{
  DDS_Entity * const entity = DDS_DomainParticipant_as_entity(dp);
  rmw_connextdds_get_entity_gid(entity, gid);
}

void rmw_connextdds_get_entity_gid(
  DDS_Publisher * const pub, rmw_gid_t & gid)
{
  DDS_Entity * const entity = DDS_Publisher_as_entity(pub);
  rmw_connextdds_get_entity_gid(entity, gid);
}

void rmw_connextdds_get_entity_gid(
  DDS_Subscriber * const sub, rmw_gid_t & gid)
{
  DDS_Entity * const entity = DDS_Subscriber_as_entity(sub);
  rmw_connextdds_get_entity_gid(entity, gid);
}

void rmw_connextdds_get_entity_gid(
  DDS_DataWriter * const writer, rmw_gid_t & gid)
{
  DDS_Entity * const entity = DDS_DataWriter_as_entity(writer);
  rmw_connextdds_get_entity_gid(entity, gid);
}

void rmw_connextdds_get_entity_gid(
  DDS_DataReader * const reader, rmw_gid_t & gid)
{
  DDS_Entity * const entity = DDS_DataReader_as_entity(reader);
  rmw_connextdds_get_entity_gid(entity, gid);
}

void rmw_connextdds_get_entity_gid(
  DDS_Topic * const topic, rmw_gid_t & gid)
{
  DDS_Entity * const entity = DDS_Topic_as_entity(topic);
  rmw_connextdds_get_entity_gid(entity, gid);
}

/******************************************************************************
 * Type helpers
 ******************************************************************************/

static
std::string
rmw_connextdds_create_type_name(
  const char * const message_namespace,
  const char * const message_name,
  const char * const message_suffix,
  const bool mangle_prefix)
{
  const char * prefix_sfx = "";
  if (mangle_prefix) {
    prefix_sfx = "_";
  }

  std::ostringstream ss;
  std::string msg_namespace(message_namespace);
  if (!msg_namespace.empty()) {
    ss << msg_namespace << "::";
  }
  ss << "dds" << prefix_sfx << "::" << message_name << message_suffix;
  return ss.str();
}

std::string
rmw_connextdds_create_type_name(
  const message_type_support_callbacks_t * callbacks,
  const bool mangle_names)
{
  const char * msg_prefix = "";
  const bool mangle_prefix = mangle_names;
  if (mangle_names) {
    msg_prefix = "_";
  }
  return rmw_connextdds_create_type_name(
    callbacks->message_namespace_,
    callbacks->message_name_,
    msg_prefix,
    mangle_prefix);
}

#if RMW_CONNEXT_HAVE_INTRO_TYPE_SUPPORT

std::string
rmw_connextdds_create_type_name(
  const rosidl_typesupport_introspection_cpp::MessageMembers * const members,
  const bool mangle_names)
{
  const char * msg_prefix = "";
  const bool mangle_prefix = mangle_names;
  if (mangle_names) {
    msg_prefix = "_";
  }
  return rmw_connextdds_create_type_name(
    members->message_namespace_,
    members->message_name_,
    msg_prefix,
    mangle_prefix);
}

std::string
rmw_connextdds_create_type_name(
  const rosidl_typesupport_introspection_c__MessageMembers * const members,
  const bool mangle_names)
{
  const char * msg_prefix = "";
  const bool mangle_prefix = mangle_names;
  if (mangle_names) {
    msg_prefix = "_";
  }
  return rmw_connextdds_create_type_name(
    members->message_namespace_,
    members->message_name_,
    msg_prefix,
    mangle_prefix);
}

#endif /* RMW_CONNEXT_HAVE_INTRO_TYPE_SUPPORT */

std::string
rmw_connextdds_create_type_name_request(
  const service_type_support_callbacks_t * callbacks,
  const bool mangle_names)
{
  const char * msg_prefix = "Request";
  const bool mangle_prefix = mangle_names;
  if (mangle_names) {
    msg_prefix = "_Request_";
  }
  return rmw_connextdds_create_type_name(
    callbacks->service_namespace_,
    callbacks->service_name_,
    msg_prefix,
    mangle_prefix);
}

std::string
rmw_connextdds_create_type_name_response(
  const service_type_support_callbacks_t * callbacks,
  const bool mangle_names)
{
  const char * msg_prefix = "Response";
  const bool mangle_prefix = mangle_names;
  if (mangle_names) {
    msg_prefix = "_Response_";
  }
  return rmw_connextdds_create_type_name(
    callbacks->service_namespace_,
    callbacks->service_name_,
    msg_prefix,
    mangle_prefix);
}

/******************************************************************************
 * Client/Service helpers
 ******************************************************************************/

RMW_Connext_Client *
RMW_Connext_Client::create(
  rmw_context_impl_t * const ctx,
  DDS_DomainParticipant * const dp,
  DDS_Publisher * const pub,
  DDS_Subscriber * const sub,
  const rosidl_service_type_support_t * const type_supports,
  const char * const svc_name,
  const rmw_qos_profile_t * const qos_policies)
{
  RMW_Connext_Client * client_impl =
    new (std::nothrow) RMW_Connext_Client();

  if (nullptr == client_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to allocate client implementation")
    return nullptr;
  }

  auto scope_exit_client_impl_delete = rcpputils::make_scope_exit(
    [client_impl]()
    {
      if (RMW_RET_OK != client_impl->finalize()) {
        RMW_CONNEXT_LOG_ERROR("failed to finalize client")
      }
      delete client_impl;
    });
  bool svc_members_req_cpp = false,
    svc_members_res_cpp = false;
  const void * svc_members_req = nullptr,
    * svc_members_res = nullptr;
  const rosidl_message_type_support_t
  * type_support_req =
    RMW_Connext_ServiceTypeSupportWrapper::get_request_type_support(
    type_supports,
    &svc_members_req,
    svc_members_req_cpp),
    * type_support_res =
    RMW_Connext_ServiceTypeSupportWrapper::get_response_type_support(
    type_supports,
    &svc_members_res,
    svc_members_res_cpp);

  if (nullptr == type_support_req || nullptr == type_support_res) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to lookup type supports for client")
    return nullptr;
  }

  std::string reply_topic =
    rmw_connextdds_create_topic_name(
    ROS_SERVICE_RESPONSE_PREFIX,
    svc_name,
    "Reply",
    qos_policies);
  std::string request_topic =
    rmw_connextdds_create_topic_name(
    ROS_SERVICE_REQUESTER_PREFIX,
    svc_name,
    "Request",
    qos_policies);

  std::string
    request_type =
    RMW_Connext_ServiceTypeSupportWrapper::get_request_type_name(
    type_supports),
    reply_type =
    RMW_Connext_ServiceTypeSupportWrapper::get_response_type_name(
    type_supports);

#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  rmw_publisher_options_t pub_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t sub_options =
    rmw_get_default_subscription_options();
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */


  RMW_CONNEXT_LOG_DEBUG_A(
    "creating request publisher: "
    "service=%s, "
    "topic=%s",
    svc_name,
    request_topic.c_str())

  client_impl->request_pub =
    RMW_Connext_Publisher::create(
    ctx,
    dp,
    pub,
    type_support_req,
    request_topic.c_str(),
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    &pub_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    false /* internal */,
    RMW_CONNEXT_MESSAGE_REQUEST,
    svc_members_req,
    svc_members_req_cpp,
    &request_type);

  if (nullptr == client_impl->request_pub) {
    RMW_CONNEXT_LOG_ERROR("failed to create client requester")
    return nullptr;
  }

  DDS_InstanceHandle_t writer_ih = client_impl->request_pub->instance_handle();
  // TODO(asorbini) convert ih directly to guid
  DDS_GUID_t writer_guid = DDS_GUID_INITIALIZER;
  rmw_gid_t writer_gid;
  rmw_connextdds_ih_to_gid(writer_ih, writer_gid);
  rmw_connextdds_gid_to_guid(writer_gid, writer_guid);

  RMW_CONNEXT_LOG_DEBUG_A(
    "creating reply subscriber: "
    "service=%s, "
    "topic=%s",
    svc_name,
    reply_topic.c_str())

#define GUID_FMT \
  "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X"

#define GUID_FMT_ARGS(g_) \
  (g_)->value[0], \
  (g_)->value[1], \
  (g_)->value[2], \
  (g_)->value[3], \
  (g_)->value[4], \
  (g_)->value[5], \
  (g_)->value[6], \
  (g_)->value[7], \
  (g_)->value[8], \
  (g_)->value[9], \
  (g_)->value[10], \
  (g_)->value[11], \
  (g_)->value[12], \
  (g_)->value[13], \
  (g_)->value[14], \
  (g_)->value[15]

  /* Create content-filtered topic expression for the reply reader */
  static const int GUID_SIZE = 16;
  static const char * GUID_FIELD_NAME =
    "@related_sample_identity.writer_guid.value";
  const size_t reply_topic_len = strlen(reply_topic.c_str());
  const size_t cft_name_len = reply_topic_len + 1 + (GUID_SIZE * 2);
  char * const cft_name = DDS_String_alloc(cft_name_len);
  if (nullptr == cft_name) {
    RMW_CONNEXT_LOG_ERROR_SET("failed too allocate cft name")
    return nullptr;
  }
  auto scope_exit_cft_name = rcpputils::make_scope_exit(
    [cft_name]()
    {
      DDS_String_free(cft_name);
    });

  snprintf(
    cft_name, cft_name_len + 1 /* \0 */,
    "%s_" GUID_FMT,
    reply_topic.c_str(),
    GUID_FMT_ARGS(&writer_guid));

  const size_t cft_filter_len =
    strlen(GUID_FIELD_NAME) + strlen(" = &hex(") + (GUID_SIZE * 2) + 1;
  char * const cft_filter = DDS_String_alloc(cft_filter_len);
  if (nullptr == cft_filter) {
    RMW_CONNEXT_LOG_ERROR_SET("failed too allocate cft filter")
    return nullptr;
  }
  auto scope_exit_cft_filter = rcpputils::make_scope_exit(
    [cft_filter]()
    {
      DDS_String_free(cft_filter);
    });

  snprintf(
    cft_filter, cft_filter_len + 1 /* \0 */, "%s = &hex(" GUID_FMT ")",
    GUID_FIELD_NAME,
    GUID_FMT_ARGS(&writer_guid));

#undef GUID_FMT
#undef GUID_FMT_ARGS

  client_impl->reply_sub =
    RMW_Connext_Subscriber::create(
    ctx,
    dp,
    sub,
    type_support_res,
    reply_topic.c_str(),
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    &sub_options,
#else
    false /* ignore_local_publications */,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    false /* internal */,
    RMW_CONNEXT_MESSAGE_REPLY,
    svc_members_res,
    svc_members_res_cpp,
    &reply_type,
    cft_name,
    cft_filter);

  if (nullptr == client_impl->reply_sub) {
    RMW_CONNEXT_LOG_ERROR("failed to create client replier")
    return nullptr;
  }

  scope_exit_client_impl_delete.cancel();
  return client_impl;
}

rmw_ret_t
RMW_Connext_Client::enable()
{
  rmw_ret_t rc = this->request_pub->enable();
  if (RMW_RET_OK != rc) {
    RMW_CONNEXT_LOG_ERROR("failed to enable client's publisher")
    return rc;
  }
  rc = this->reply_sub->enable();
  if (RMW_RET_OK != rc) {
    RMW_CONNEXT_LOG_ERROR("failed to enable client's subscription")
    return rc;
  }
  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_Client::is_service_available(bool & available)
{
  /* TODO(asorbini): check that we actually have at least one service matched by both
     request writer and response reader */
  available = (this->request_pub->subscriptions_count() > 0 &&
    this->reply_sub->publications_count() > 0);
  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_Client::take_response(
  rmw_service_info_t * const request_header,
  void * const ros_response,
  bool * const taken)
{
  *taken = false;

  RMW_Connext_RequestReplyMessage rr_msg;
  rr_msg.request = false;
  rr_msg.payload = ros_response;

  rmw_message_info_t message_info;
  bool taken_msg = false;

  DDS_InstanceHandle_t req_writer_handle =
    this->request_pub->instance_handle();

  rmw_ret_t rc =
    this->reply_sub->take_message(
    &rr_msg, &message_info, &taken_msg, &req_writer_handle);

  if (RMW_RET_OK != rc) {
    return rc;
  }

  if (taken_msg) {
    request_header->request_id.sequence_number = rr_msg.sn;
    memcpy(
      request_header->request_id.writer_guid,
      rr_msg.gid.data,
      16);
// Message timestamps are disabled on Windows because RTI Connext DDS
// does not support a high enough clock resolution by default (see: _ftime()).
#if RMW_CONNEXT_HAVE_MESSAGE_INFO_TS && !RTI_WIN32
    request_header->source_timestamp = message_info.source_timestamp;
    request_header->received_timestamp = message_info.received_timestamp;
#endif /* RMW_CONNEXT_HAVE_MESSAGE_INFO_TS */

    *taken = true;

    RMW_CONNEXT_LOG_DEBUG_A(
      "[%s] taken RESPONSE: "
      "gid=%08X.%08X.%08X.%08X, "
      "sn=%lu",
      this->reply_sub->message_type_support()->type_name(),
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[0],
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[1],
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[2],
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[3],
      rr_msg.sn)
  }

  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_Client::send_request(
  const void * const ros_request,
  int64_t * const sequence_id)
{
  RMW_Connext_RequestReplyMessage rr_msg;
  rr_msg.request = true;

#if RMW_CONNEXT_EMULATE_REQUESTREPLY
  *sequence_id = ++this->next_request_id;
  rr_msg.sn = *sequence_id;
#else
  rr_msg.sn = -1;
#endif /* RMW_CONNEXT_EMULATE_REQUESTREPLY */
  rr_msg.gid = *this->request_pub->gid();
  rr_msg.payload = const_cast<void *>(ros_request);

  RMW_CONNEXT_LOG_DEBUG_A(
    "[%s] send REQUEST: "
    "gid=%08X.%08X.%08X.%08X, "
    "sn=%lu",
    this->request_pub->message_type_support()->type_name(),
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[0],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[1],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[2],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[3],
    rr_msg.sn)

  rmw_ret_t rc = this->request_pub->write(&rr_msg, false /* serialized */, sequence_id);

  RMW_CONNEXT_LOG_DEBUG_A(
    "[%s] SENT REQUEST: "
    "gid=%08X.%08X.%08X.%08X, "
    "sn=%lu",
    this->request_pub->message_type_support()->type_name(),
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[0],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[1],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[2],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[3],
    *sequence_id)

  return rc;
}

rmw_ret_t
RMW_Connext_Client::finalize()
{
  if (nullptr != this->request_pub) {
    if (RMW_RET_OK != this->request_pub->finalize()) {
      RMW_CONNEXT_LOG_ERROR("failed to finalize client publisher")
      return RMW_RET_ERROR;
    }
    delete this->request_pub;
  }

  if (nullptr != this->reply_sub) {
    if (RMW_RET_OK != this->reply_sub->finalize()) {
      RMW_CONNEXT_LOG_ERROR("failed to finalize client subscriber")
      return RMW_RET_ERROR;
    }

    delete this->reply_sub;
  }

  return RMW_RET_OK;
}


RMW_Connext_Service *
RMW_Connext_Service::create(
  rmw_context_impl_t * const ctx,
  DDS_DomainParticipant * const dp,
  DDS_Publisher * const pub,
  DDS_Subscriber * const sub,
  const rosidl_service_type_support_t * const type_supports,
  const char * const svc_name,
  const rmw_qos_profile_t * const qos_policies)
{
  RMW_Connext_Service * svc_impl =
    new (std::nothrow) RMW_Connext_Service();

  if (nullptr == svc_impl) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to allocate service implementation")
    return nullptr;
  }

  auto scope_exit_svc_impl_delete = rcpputils::make_scope_exit(
    [svc_impl]()
    {
      delete svc_impl;
    });

  bool svc_members_req_cpp = false,
    svc_members_res_cpp = false;
  const void * svc_members_req = nullptr,
    * svc_members_res = nullptr;
  const rosidl_message_type_support_t
  * type_support_req =
    RMW_Connext_ServiceTypeSupportWrapper::get_request_type_support(
    type_supports,
    &svc_members_req,
    svc_members_req_cpp),
    * type_support_res =
    RMW_Connext_ServiceTypeSupportWrapper::get_response_type_support(
    type_supports,
    &svc_members_res,
    svc_members_res_cpp);

  if (nullptr == type_support_req || nullptr == type_support_res) {
    RMW_CONNEXT_LOG_ERROR_SET("failed to lookup type supports for service")
    return nullptr;
  }

  std::string reply_topic =
    rmw_connextdds_create_topic_name(
    ROS_SERVICE_RESPONSE_PREFIX,
    svc_name,
    "Reply",
    qos_policies);
  std::string request_topic =
    rmw_connextdds_create_topic_name(
    ROS_SERVICE_REQUESTER_PREFIX,
    svc_name,
    "Request",
    qos_policies);

  std::string
    request_type =
    RMW_Connext_ServiceTypeSupportWrapper::get_request_type_name(
    type_supports),
    reply_type =
    RMW_Connext_ServiceTypeSupportWrapper::get_response_type_name(
    type_supports);

#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
  rmw_publisher_options_t pub_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t sub_options =
    rmw_get_default_subscription_options();
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */

  RMW_CONNEXT_LOG_DEBUG_A(
    "creating reply publisher: "
    "service=%s, "
    "topic=%s",
    svc_name,
    reply_topic.c_str())

  svc_impl->reply_pub =
    RMW_Connext_Publisher::create(
    ctx,
    dp,
    pub,
    type_support_res,
    reply_topic.c_str(),
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    &pub_options,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    false /* internal */,
    RMW_CONNEXT_MESSAGE_REPLY,
    svc_members_res,
    svc_members_res_cpp,
    &reply_type);

  if (nullptr == svc_impl->reply_pub) {
    RMW_CONNEXT_LOG_ERROR("failed to create service replier")
    return nullptr;
  }

  RMW_CONNEXT_LOG_DEBUG_A(
    "creating request subscriber: "
    "service=%s, "
    "topic=%s",
    svc_name,
    request_topic.c_str())

  svc_impl->request_sub =
    RMW_Connext_Subscriber::create(
    ctx,
    dp,
    sub,
    type_support_req,
    request_topic.c_str(),
    qos_policies,
#if RMW_CONNEXT_HAVE_OPTIONS_PUBSUB
    &sub_options,
#else
    false /* ignore_local_publications */,
#endif /* RMW_CONNEXT_HAVE_OPTIONS_PUBSUB */
    false /* internal */,
    RMW_CONNEXT_MESSAGE_REQUEST,
    svc_members_req,
    svc_members_req_cpp,
    &request_type);

  if (nullptr == svc_impl->request_sub) {
    RMW_CONNEXT_LOG_ERROR("failed to create service requester")
    return nullptr;
  }

  scope_exit_svc_impl_delete.cancel();
  return svc_impl;
}

rmw_ret_t
RMW_Connext_Service::enable()
{
  rmw_ret_t rc = this->reply_pub->enable();
  if (RMW_RET_OK != rc) {
    RMW_CONNEXT_LOG_ERROR("failed to enable service's publisher")
    return rc;
  }
  rc = this->request_sub->enable();
  if (RMW_RET_OK != rc) {
    RMW_CONNEXT_LOG_ERROR("failed to enable service's subscription")
    return rc;
  }
  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_Service::take_request(
  rmw_service_info_t * const request_header,
  void * const ros_request,
  bool * const taken)
{
  *taken = false;

  RMW_Connext_RequestReplyMessage rr_msg;
  rr_msg.request = true;
  rr_msg.payload = ros_request;

  rmw_message_info_t message_info;
  bool taken_msg = false;

  rmw_ret_t rc =
    this->request_sub->take_message(
    &rr_msg, &message_info, &taken_msg);

  if (RMW_RET_OK != rc) {
    return rc;
  }

  if (taken_msg) {
    request_header->request_id.sequence_number = rr_msg.sn;

    memcpy(
      request_header->request_id.writer_guid,
      rr_msg.gid.data,
      16);
// Message timestamps are disabled on Windows because RTI Connext DDS
// does not support a high enough clock resolution by default (see: _ftime()).
#if RMW_CONNEXT_HAVE_MESSAGE_INFO_TS && !RTI_WIN32
    request_header->source_timestamp = message_info.source_timestamp;
    request_header->received_timestamp = message_info.received_timestamp;
#endif /* RMW_CONNEXT_HAVE_MESSAGE_INFO_TS */

    *taken = true;

    RMW_CONNEXT_LOG_DEBUG_A(
      "[%s] taken REQUEST: "
      "gid=%08X.%08X.%08X.%08X, "
      "sn=%lu",
      this->request_sub->message_type_support()->type_name(),
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[0],
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[1],
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[2],
      reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[3],
      rr_msg.sn)
  }

  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_Service::send_response(
  rmw_request_id_t * const request_id,
  const void * const ros_response)
{
  RMW_Connext_RequestReplyMessage rr_msg;
  rr_msg.request = false;
  rr_msg.sn = request_id->sequence_number;
  memcpy(rr_msg.gid.data, request_id->writer_guid, 16);
  rr_msg.gid.implementation_identifier = RMW_CONNEXTDDS_ID;
  rr_msg.payload = const_cast<void *>(ros_response);

  RMW_CONNEXT_LOG_DEBUG_A(
    "[%s] send RESPONSE: "
    "gid=%08X.%08X.%08X.%08X, "
    "sn=%lu",
    this->reply_pub->message_type_support()->type_name(),
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[0],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[1],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[2],
    reinterpret_cast<const uint32_t *>(rr_msg.gid.data)[3],
    rr_msg.sn)

  return this->reply_pub->write(&rr_msg, false /* serialized */);
}

rmw_ret_t
RMW_Connext_Service::finalize()
{
  if (RMW_RET_OK != this->publisher()->finalize()) {
    RMW_CONNEXT_LOG_ERROR("failed to finalize service publisher")
    return RMW_RET_ERROR;
  }

  delete this->publisher();

  if (RMW_RET_OK != this->subscriber()->finalize()) {
    RMW_CONNEXT_LOG_ERROR("failed to finalize service subscriber")
    return RMW_RET_ERROR;
  }

  delete this->subscriber();

  return RMW_RET_OK;
}

/******************************************************************************
 * Event helpers
 ******************************************************************************/

DDS_StatusKind
ros_event_to_dds(const rmw_event_type_t ros, bool * const invalid)
{
  if (nullptr != invalid) {
    *invalid = false;
  }
  switch (ros) {
    case RMW_EVENT_LIVELINESS_CHANGED:
      {
        return DDS_LIVELINESS_CHANGED_STATUS;
      }
    case RMW_EVENT_REQUESTED_DEADLINE_MISSED:
      {
        return DDS_REQUESTED_DEADLINE_MISSED_STATUS;
      }
    case RMW_EVENT_LIVELINESS_LOST:
      {
        return DDS_LIVELINESS_LOST_STATUS;
      }
    case RMW_EVENT_OFFERED_DEADLINE_MISSED:
      {
        return DDS_OFFERED_DEADLINE_MISSED_STATUS;
      }
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
      {
        return DDS_REQUESTED_INCOMPATIBLE_QOS_STATUS;
      }
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
      {
        return DDS_OFFERED_INCOMPATIBLE_QOS_STATUS;
      }
// Avoid warnings caused by RMW_EVENT_MESSAGE_LOST not being one of
// the defined values for rmw_event_type_t. This #if and the one in
// the `default` case, should be removed once support for releases
// without RMW_EVENT_MESSAGE_LOST is dropped (or the value is backported).
#if RMW_CONNEXT_HAVE_MESSAGE_LOST
    case RMW_EVENT_MESSAGE_LOST:
      {
        return DDS_SAMPLE_LOST_STATUS;
      }
#endif /* RMW_CONNEXT_HAVE_MESSAGE_LOST */
    default:
      {
#if !RMW_CONNEXT_HAVE_MESSAGE_LOST
        if (RMW_EVENT_MESSAGE_LOST == ros) {
          RMW_CONNEXT_LOG_WARNING(
            "unexpected rmw_event_type_t: RMW_EVENT_MESSAGE_LOST")
        }
#endif /* !RMW_CONNEXT_HAVE_MESSAGE_LOST */
        if (nullptr != invalid) {
          *invalid = true;
        }
        return (DDS_StatusKind)UINT32_MAX;
      }
  }
}

const char *
dds_event_to_str(const DDS_StatusKind event)
{
  switch (event) {
    case DDS_LIVELINESS_CHANGED_STATUS:
      {
        return "LIVELINESS_CHANGED";
      }
    case DDS_REQUESTED_DEADLINE_MISSED_STATUS:
      {
        return "REQUESTED_DEADLINE_MISSED";
      }
    case DDS_LIVELINESS_LOST_STATUS:
      {
        return "LIVELINESS_LOST";
      }
    case DDS_OFFERED_DEADLINE_MISSED_STATUS:
      {
        return "OFFERED_DEADLINE_MISSED";
      }
    case DDS_REQUESTED_INCOMPATIBLE_QOS_STATUS:
      {
        return "REQUESTED_INCOMPATIBLE_QOS";
      }
    case DDS_OFFERED_INCOMPATIBLE_QOS_STATUS:
      {
        return "OFFERED_INCOMPATIBLE_QOS";
      }
    case DDS_SAMPLE_LOST_STATUS:
      {
        return "SAMPLE_LOST";
      }
    default:
      {
        return "UNSUPPORTED";
      }
  }
}

bool
ros_event_for_reader(const rmw_event_type_t ros)
{
  switch (ros) {
    case RMW_EVENT_LIVELINESS_CHANGED:
    case RMW_EVENT_REQUESTED_DEADLINE_MISSED:
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
// Avoid warnings caused by RMW_EVENT_MESSAGE_LOST not being one of
// the defined values for rmw_event_type_t. This #if and the one in
// the `default` case, should be removed once support for releases
// without RMW_EVENT_MESSAGE_LOST is dropped (or the value is backported).
#if RMW_CONNEXT_HAVE_MESSAGE_LOST
    case RMW_EVENT_MESSAGE_LOST:
#endif /* RMW_CONNEXT_HAVE_MESSAGE_LOST */
      {
        return true;
      }
    default:
      {
#if !RMW_CONNEXT_HAVE_MESSAGE_LOST
        if (RMW_EVENT_MESSAGE_LOST == ros) {
          RMW_CONNEXT_LOG_WARNING(
            "unexpected rmw_event_type_t: RMW_EVENT_MESSAGE_LOST")
        }
#endif /* !RMW_CONNEXT_HAVE_MESSAGE_LOST */
        return false;
      }
  }
}

/******************************************************************************
 * StdWaitSet
 ******************************************************************************/

template<typename T>
bool
RMW_Connext_WaitSet::require_attach(
  const std::vector<T *> & attached_els,
  const size_t new_els_count,
  void ** const new_els)
{
  if (nullptr == new_els || 0 == new_els_count) {
    return attached_els.size() > 0;
  } else if (new_els_count != attached_els.size()) {
    return true;
  } else {
    const void * const attached_data =
      static_cast<const void *>(attached_els.data());
    void * const new_els_data = static_cast<void *>(new_els);

    const size_t cmp_size = new_els_count * sizeof(void *);

    return memcmp(attached_data, new_els_data, cmp_size) != 0;
  }
}

rmw_ret_t
RMW_Connext_WaitSet::detach()
{
  bool failed = false;

  for (auto && sub : this->attached_subscribers) {
    RMW_Connext_StatusCondition * const cond = sub->condition();
    {
      std::lock_guard<std::mutex> lock(cond->mutex_internal);
      rmw_ret_t rc = cond->detach();
      if (RMW_RET_OK != rc) {
        RMW_CONNEXT_LOG_ERROR("failed to detach subscriber's condition")
        failed = true;
      }
    }
  }
  this->attached_subscribers.clear();

  for (auto && gc : this->attached_conditions) {
    {
      std::lock_guard<std::mutex> lock(gc->mutex_internal);
      rmw_ret_t rc = gc->detach();
      if (RMW_RET_OK != rc) {
        RMW_CONNEXT_LOG_ERROR("failed to detach guard condition")
        failed = true;
      }
    }
  }
  this->attached_conditions.clear();

  for (auto && client : this->attached_clients) {
    RMW_Connext_StatusCondition * const cond = client->subscriber()->condition();
    {
      std::lock_guard<std::mutex> lock(cond->mutex_internal);
      rmw_ret_t rc = cond->detach();
      if (RMW_RET_OK != rc) {
        RMW_CONNEXT_LOG_ERROR("failed to detach client's condition")
        failed = true;
      }
    }
  }
  this->attached_clients.clear();

  for (auto && service : this->attached_services) {
    RMW_Connext_StatusCondition * const cond = service->subscriber()->condition();
    {
      std::lock_guard<std::mutex> lock(cond->mutex_internal);
      rmw_ret_t rc = cond->detach();
      if (RMW_RET_OK != rc) {
        RMW_CONNEXT_LOG_ERROR("failed to detach service's condition")
        failed = true;
      }
    }
  }
  this->attached_services.clear();

  for (auto && e : this->attached_events) {
    auto e_cached = this->attached_events_cache[e];
    RMW_Connext_StatusCondition * const cond = RMW_Connext_Event::condition(&e_cached);
    {
      std::lock_guard<std::mutex> lock(cond->mutex_internal);
      rmw_ret_t rc = cond->detach();
      if (RMW_RET_OK != rc) {
        RMW_CONNEXT_LOG_ERROR("failed to detach event's condition")
        failed = true;
      }
    }
  }
  this->attached_events.clear();
  this->attached_events_cache.clear();

  return failed ? RMW_RET_ERROR : RMW_RET_OK;
}


rmw_ret_t
RMW_Connext_WaitSet::attach(
  rmw_subscriptions_t * const subs,
  rmw_guard_conditions_t * const gcs,
  rmw_services_t * const srvs,
  rmw_clients_t * const cls,
  rmw_events_t * const evs)
{
  const bool refresh_attach_subs =
    this->require_attach(
    this->attached_subscribers,
    (nullptr != subs) ? subs->subscriber_count : 0,
    (nullptr != subs) ? subs->subscribers : nullptr),
    refresh_attach_gcs =
    this->require_attach(
    this->attached_conditions,
    (nullptr != gcs) ? gcs->guard_condition_count : 0,
    (nullptr != gcs) ? gcs->guard_conditions : nullptr),
    refresh_attach_srvs =
    this->require_attach(
    this->attached_services,
    (nullptr != srvs) ? srvs->service_count : 0,
    (nullptr != srvs) ? srvs->services : nullptr),
    refresh_attach_cls =
    this->require_attach(
    this->attached_clients,
    (nullptr != cls) ? cls->client_count : 0,
    (nullptr != cls) ? cls->clients : nullptr),
    refresh_attach_evs =
    this->require_attach(
    this->attached_events,
    (nullptr != evs) ? evs->event_count : 0,
    (nullptr != evs) ? evs->events : nullptr);
  const bool refresh_attach =
    refresh_attach_subs ||
    refresh_attach_gcs ||
    refresh_attach_evs ||
    refresh_attach_srvs ||
    refresh_attach_cls;

  if (!refresh_attach) {
    // nothing to do since lists of attached elements didn't change
    return RMW_RET_OK;
  }

  rmw_ret_t rc = this->detach();
  if (RMW_RET_OK != rc) {
    RMW_CONNEXT_LOG_ERROR("failed to detach conditions from waitset")
    return rc;
  }

  // First iterate over events, and reset the "enabled statuses" of the
  // target entity's status condition. We could skip any subscriber that is
  // also passed in for data (since the "enabled statuses" will be reset to
  // DATA_AVAILABLE only for these subscribers), but we reset them anyway to
  // avoid having to search the subscriber's list for each one.
  if (nullptr != evs) {
    for (size_t i = 0; i < evs->event_count; ++i) {
      rmw_event_t * const event =
        reinterpret_cast<rmw_event_t *>(evs->events[i]);
      RMW_Connext_StatusCondition * const cond =
        RMW_Connext_Event::condition(event);
      RMW_Connext_WaitSet * const otherws = cond->attached_waitset;
      bool detached = false;
      if (nullptr != otherws && this != otherws) {
        otherws->invalidate(cond);
        detached = true;
      }
      {
        std::lock_guard<std::mutex> lock(cond->mutex_internal);
        if (cond->deleted) {
          return RMW_RET_ERROR;
        }
        if (detached) {
          cond->attached_waitset = nullptr;
        }
        rmw_ret_t rc = cond->reset_statuses();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to reset event's condition")
          return rc;
        }
      }
    }
  }

  if (nullptr != subs) {
    for (size_t i = 0; i < subs->subscriber_count; ++i) {
      RMW_Connext_Subscriber * const sub =
        reinterpret_cast<RMW_Connext_Subscriber *>(subs->subscribers[i]);
      RMW_Connext_SubscriberStatusCondition * const cond = sub->condition();
      RMW_Connext_WaitSet * const otherws = cond->attached_waitset;
      bool detached = false;
      if (nullptr != otherws && this != otherws) {
        otherws->invalidate(cond);
        detached = true;
      }
      {
        std::lock_guard<std::mutex> lock(cond->mutex_internal);
        if (cond->deleted) {
          return RMW_RET_ERROR;
        }
        if (detached) {
          cond->attached_waitset = nullptr;
        }
        rmw_ret_t rc = cond->reset_statuses();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to reset subscriber's condition")
          return rc;
        }
        rc = cond->enable_statuses(DDS_DATA_AVAILABLE_STATUS);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to enable subscriber's condition")
          return rc;
        }
        rc = cond->attach(this);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach subscriber's condition")
          return rc;
        }
        rc = cond->attach_data();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach subscriber's data condition")
          return rc;
        }
      }
      this->attached_subscribers.push_back(sub);
    }
  }

  if (nullptr != cls) {
    for (size_t i = 0; i < cls->client_count; ++i) {
      RMW_Connext_Client * const client =
        reinterpret_cast<RMW_Connext_Client *>(cls->clients[i]);
      RMW_Connext_SubscriberStatusCondition * const cond = client->subscriber()->condition();
      RMW_Connext_WaitSet * const otherws = cond->attached_waitset;
      bool detached = false;
      if (nullptr != otherws && this != otherws) {
        otherws->invalidate(cond);
        detached = true;
      }
      {
        std::lock_guard<std::mutex> lock(cond->mutex_internal);
        if (cond->deleted) {
          return RMW_RET_ERROR;
        }
        if (detached) {
          cond->attached_waitset = nullptr;
        }
        rmw_ret_t rc = cond->reset_statuses();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to reset subscriber's condition")
          return rc;
        }
        rc = cond->enable_statuses(DDS_DATA_AVAILABLE_STATUS);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to enable client's condition")
          return rc;
        }
        rc = cond->attach(this);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach client's condition")
          return rc;
        }
        rc = cond->attach_data();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach client's data condition")
          return rc;
        }
      }
      this->attached_clients.push_back(client);
    }
  }

  if (nullptr != srvs) {
    for (size_t i = 0; i < srvs->service_count; ++i) {
      RMW_Connext_Service * const svc =
        reinterpret_cast<RMW_Connext_Service *>(srvs->services[i]);
      RMW_Connext_SubscriberStatusCondition * const cond = svc->subscriber()->condition();
      RMW_Connext_WaitSet * const otherws = cond->attached_waitset;
      bool detached = false;
      if (nullptr != otherws && this != otherws) {
        otherws->invalidate(cond);
        detached = true;
      }
      {
        std::lock_guard<std::mutex> lock(cond->mutex_internal);
        if (cond->deleted) {
          return RMW_RET_ERROR;
        }
        if (detached) {
          cond->attached_waitset = nullptr;
        }
        rmw_ret_t rc = cond->reset_statuses();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to reset subscriber's condition")
          return rc;
        }
        rc = cond->enable_statuses(DDS_DATA_AVAILABLE_STATUS);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to enable service's condition")
          return rc;
        }
        rc = cond->attach(this);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach service's condition")
          return rc;
        }
        rc = cond->attach_data();
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach service's data condition")
          return rc;
        }
      }
      this->attached_services.push_back(svc);
    }
  }

  if (nullptr != evs) {
    for (size_t i = 0; i < evs->event_count; ++i) {
      rmw_event_t * const event =
        reinterpret_cast<rmw_event_t *>(evs->events[i]);
      RMW_Connext_StatusCondition * const cond =
        RMW_Connext_Event::condition(event);
      {
        std::lock_guard<std::mutex> lock(cond->mutex_internal);
        if (cond->deleted) {
          return RMW_RET_ERROR;
        }
        const DDS_StatusKind evt = ros_event_to_dds(event->event_type, nullptr);
        rmw_ret_t rc = cond->enable_statuses(evt);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to enable event's condition")
          return rc;
        }
        rc = cond->attach(this);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach event's condition")
          return rc;
        }
      }
      this->attached_events.push_back(event);
      // Cache a shallow copy of the rmw_event_t structure so that we may
      // access it safely during detach(), even if the original event has been
      // deleted already by that time.
      this->attached_events_cache.emplace(event, *event);
    }
  }

  if (nullptr != gcs) {
    for (size_t i = 0; i < gcs->guard_condition_count; ++i) {
      RMW_Connext_GuardCondition * const gcond =
        reinterpret_cast<RMW_Connext_GuardCondition *>(gcs->guard_conditions[i]);
      RMW_Connext_WaitSet * const otherws = gcond->attached_waitset;
      bool detached = false;
      if (nullptr != otherws && this != otherws) {
        otherws->invalidate(gcond);
        detached = true;
      }
      {
        std::lock_guard<std::mutex> lock(gcond->mutex_internal);
        if (gcond->deleted) {
          return RMW_RET_ERROR;
        }
        if (detached) {
          gcond->attached_waitset = nullptr;
        }
        rmw_ret_t rc = gcond->attach(this);
        if (RMW_RET_OK != rc) {
          RMW_CONNEXT_LOG_ERROR("failed to attach guard condition")
          return rc;
        }
      }
      this->attached_conditions.push_back(gcond);
    }
  }
  return RMW_RET_OK;
}

bool
RMW_Connext_WaitSet::active_condition(RMW_Connext_Condition * const cond)
{
  DDS_Long active_len = DDS_ConditionSeq_get_length(&this->active_conditions);
  bool active = false;

  for (DDS_Long i = 0; i < active_len && !active; i++) {
    DDS_Condition * const acond =
      *DDS_ConditionSeq_get_reference(&this->active_conditions, i);
    active = cond->owns(acond);
  }

  return active;
}

bool
RMW_Connext_WaitSet::is_attached(RMW_Connext_Condition * const cond)
{
  for (auto && sub : this->attached_subscribers) {
    if (sub->condition() == cond) {
      return true;
    }
  }

  for (auto && gc : this->attached_conditions) {
    if (gc == cond) {
      return true;
    }
  }

  for (auto && client : this->attached_clients) {
    if (client->subscriber()->condition() == cond) {
      return true;
    }
  }

  for (auto && service : this->attached_services) {
    if (service->subscriber()->condition() == cond) {
      return true;
    }
  }

  for (auto && e : this->attached_events) {
    auto e_cached = this->attached_events_cache[e];
    if (RMW_Connext_Event::condition(&e_cached) == cond) {
      return true;
    }
  }

  return false;
}

rmw_ret_t
RMW_Connext_WaitSet::process_wait(
  rmw_subscriptions_t * const subs,
  rmw_guard_conditions_t * const gcs,
  rmw_services_t * const srvs,
  rmw_clients_t * const cls,
  rmw_events_t * const evs,
  size_t & active_conditions)
{
  bool failed = false;
  size_t i = 0;

  // If any of the attached conditions has become "invalid" while we were
  // waiting, we will finish processing the results, and detach all existing
  // conditions at the end, to make sure that no stale references is stored by
  // the waitset after returning from the wait() call.
  bool valid = true;

  i = 0;
  for (auto && sub : this->attached_subscribers) {
    // Check if the subscriber has some data already cached from the DataReader,
    // or check the DataReader's cache and loan samples if needed. If empty,
    // remove subscriber from returned list.
    if (!sub->has_data()) {
      subs->subscribers[i] = nullptr;
    } else {
      active_conditions += 1;
    }
    i += 1;
    valid = valid && !sub->condition()->deleted;
  }

  i = 0;
  for (auto && gc : this->attached_conditions) {
    // Scan active conditions returned by wait() looking for this guard condition
    if (!this->active_condition(gc)) {
      gcs->guard_conditions[i] = nullptr;
    } else {
      // reset conditions's trigger value. There is a risk of "race condition"
      // here since resetting the trigger value might overwrite a positive
      // trigger set by the upstream. In general, the DDS API expects guard
      // conditions to be fully managed by the application, exactly to avoid
      // this type of (pretty much unsolvable) issue (hence why
      // DDS_WaitSet_wait() will not automatically reset the trigger value of
      // an attached guard conditions).
      if (RMW_RET_OK != gc->reset_trigger()) {
        failed = true;
      }
      active_conditions += 1;
    }
    i += 1;
    valid = valid && !gc->deleted;
  }

  i = 0;
  for (auto && client : this->attached_clients) {
    if (!client->subscriber()->has_data()) {
      cls->clients[i] = nullptr;
    } else {
      active_conditions += 1;
    }
    i += 1;
    valid = valid && !client->subscriber()->condition()->deleted;
  }

  i = 0;
  for (auto && service : this->attached_services) {
    if (!service->subscriber()->has_data()) {
      srvs->services[i] = nullptr;
    } else {
      active_conditions += 1;
    }
    i += 1;
  }

  i = 0;
  for (auto && e : this->attached_events) {
    auto e_cached = this->attached_events_cache[e];
    // Check if associated DDS status is active on the associated entity
    if (!RMW_Connext_Event::active(&e_cached)) {
      evs->events[i] = nullptr;
    } else {
      active_conditions += 1;
    }
    i += 1;
    valid = valid && !RMW_Connext_Event::condition(&e_cached)->deleted;
  }

  if (!failed && valid) {
    return RMW_RET_OK;
  }

  return RMW_RET_ERROR;
}

rmw_ret_t
RMW_Connext_WaitSet::wait(
  rmw_subscriptions_t * const subs,
  rmw_guard_conditions_t * const gcs,
  rmw_services_t * const srvs,
  rmw_clients_t * const cls,
  rmw_events_t * const evs,
  const rmw_time_t * const wait_timeout)
{
  {
    std::unique_lock<std::mutex> lock(this->mutex_internal);
    bool already_taken = false;
    switch (this->state) {
      case RMW_CONNEXT_WAITSET_FREE:
        {
          // waitset is available
          break;
        }
      case RMW_CONNEXT_WAITSET_INVALIDATING:
        {
          // waitset is currently being invalidated, wait for other thread to
          // complete.
          this->state_cond.wait(lock);
          already_taken = (RMW_CONNEXT_WAITSET_FREE != this->state);
          break;
        }
      default:
        {
          already_taken = true;
          break;
        }
    }
    if (already_taken) {
      // waitset is owned by another thread
      RMW_CONNEXT_LOG_ERROR_SET(
        "multiple concurrent wait()s not supported");
      return RMW_RET_ERROR;
    }
    this->state = RMW_CONNEXT_WAITSET_ACQUIRING;
  }
  // Notify condition variable of state transition
  this->state_cond.notify_all();

  RMW_Connext_WaitSet * const ws = this;
  // If we return with an error, then try to detach all conditions to leave
  // the waitset in a "clean" state.
  auto scope_exit_detach = rcpputils::make_scope_exit(
    [ws]()
    {
      if (RMW_RET_OK != ws->detach()) {
        RMW_CONNEXT_LOG_ERROR("failed to detach conditions from waitset")
      }
    });

  // After handling a possible error condition (i.e. clearing the waitset),
  // transition back to "FREE" state.
  auto scope_exit = rcpputils::make_scope_exit(
    [ws]()
    {
      // transition waitset back to FREE state on exit
      {
        std::lock_guard<std::mutex> lock(ws->mutex_internal);
        ws->state = RMW_CONNEXT_WAITSET_FREE;
      }
      // Notify condition variable of state transition
      ws->state_cond.notify_all();
    });

  rmw_ret_t rc = this->attach(subs, gcs, srvs, cls, evs);
  if (RMW_RET_OK != rc) {
    return rc;
  }

  const size_t attached_count =
    this->attached_subscribers.size() +
    this->attached_conditions.size() +
    this->attached_clients.size() +
    this->attached_services.size() +
    this->attached_events.size();

  if (attached_count > INT32_MAX) {
    RMW_CONNEXT_LOG_ERROR("too many conditions attached to waitset")
    return RMW_RET_ERROR;
  }

  if (!DDS_ConditionSeq_ensure_length(
      &this->active_conditions,
      static_cast<DDS_Long>(attached_count),
      static_cast<DDS_Long>(attached_count)))
  {
    RMW_CONNEXT_LOG_ERROR("failed to resize conditions sequence")
    return RMW_RET_ERROR;
  }

  DDS_Duration_t wait_duration = DDS_DURATION_INFINITE;
  if (nullptr != wait_timeout) {
    rc = rmw_connextdds_duration_from_ros_time(&wait_duration, wait_timeout);
    if (RMW_RET_OK != rc) {
      return rc;
    }
  }

  // transition to state BLOCKED
  {
    std::lock_guard<std::mutex> lock(this->mutex_internal);
    this->state = RMW_CONNEXT_WAITSET_BLOCKED;
  }
  // Notify condition variable of state transition
  this->state_cond.notify_all();

  const DDS_ReturnCode_t wait_rc =
    DDS_WaitSet_wait(this->waitset, &this->active_conditions, &wait_duration);

  if (DDS_RETCODE_OK != wait_rc && DDS_RETCODE_TIMEOUT != wait_rc) {
    RMW_CONNEXT_LOG_ERROR_A_SET("DDS wait failed: %d", wait_rc)
    return RMW_RET_ERROR;
  }

  // transition to state RELEASING
  {
    std::lock_guard<std::mutex> lock(this->mutex_internal);
    this->state = RMW_CONNEXT_WAITSET_RELEASING;
  }
  // Notify condition variable of state transition
  this->state_cond.notify_all();

  size_t active_conditions = 0;
  rc = this->process_wait(subs, gcs, srvs, cls, evs, active_conditions);
  if (RMW_RET_OK != rc) {
    RMW_CONNEXT_LOG_ERROR("failed to process wait result")
    return rc;
  }

  scope_exit_detach.cancel();

  RMW_CONNEXT_ASSERT(active_conditions > 0 || DDS_RETCODE_TIMEOUT == wait_rc)

  if (DDS_RETCODE_TIMEOUT == wait_rc) {
    rmw_reset_error();
    RMW_SET_ERROR_MSG("DDS wait timed out");
    return RMW_RET_TIMEOUT;
  }

  return RMW_RET_OK;
}


rmw_ret_t
RMW_Connext_WaitSet::invalidate(RMW_Connext_Condition * const condition)
{
  std::unique_lock<std::mutex> lock(this->mutex_internal);

  // Scan attached elements to see if condition is still attached.
  // If the invalidated condition is not attached, then there's nothing to do,
  // since the waitset is already free from potential stale references.
  if (!this->is_attached(condition)) {
    return RMW_RET_OK;
  }

  // If the waitset is "FREE" then we can just mark it as "INVALIDATING",
  // do the clean up, and release it. A wait()'ing thread will detect the
  // "INVALIDATING" state and block until notified.
  if (this->state == RMW_CONNEXT_WAITSET_FREE) {
    this->state = RMW_CONNEXT_WAITSET_INVALIDATING;
    lock.unlock();

    rmw_ret_t rc = this->detach();
    if (RMW_RET_OK != rc) {
      RMW_CONNEXT_LOG_ERROR("failed to detach conditions on invalidate")
    }

    lock.lock();
    this->state = RMW_CONNEXT_WAITSET_FREE;
    lock.unlock();
    this->state_cond.notify_all();
    return rc;
  }

  // Waitset is currently inside a wait() call. If the state is not "ACQUIRING"
  // then it means the user is trying to delete a condition while simultaneously
  // waiting on it. This is an error.
  if (this->state != RMW_CONNEXT_WAITSET_ACQUIRING) {
    RMW_CONNEXT_LOG_ERROR_SET("cannot delete and wait on the same object")
    return RMW_RET_ERROR;
  }

  // Block on state_cond and wait for the next state transition, at which
  // point the condition must have been detached, or we can return an error.
  this->state_cond.wait(lock);

  if (this->is_attached(condition)) {
    RMW_CONNEXT_LOG_ERROR_SET("deleted condition not detached")
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_SubscriberStatusCondition::install()
{
  DDS_DataReaderListener listener = DDS_DataReaderListener_INITIALIZER;
  DDS_StatusMask listener_mask = DDS_STATUS_MASK_NONE;

  listener.as_listener.listener_data = this;

  rmw_connextdds_configure_subscriber_condition_listener(
    this, &listener, &listener_mask);

  // TODO(asorbini) only call set_listener() if actually setting something?
  if (DDS_RETCODE_OK !=
    DDS_DataReader_set_listener(this->reader, &listener, listener_mask))
  {
    RMW_CONNEXT_LOG_ERROR_SET("failed to configure reader listener")
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_SubscriberStatusCondition::get_status(
  const rmw_event_type_t event_type, void * const event_info)
{
  switch (event_type) {
    case RMW_EVENT_LIVELINESS_CHANGED:
      {
        rmw_liveliness_changed_status_t * status =
          reinterpret_cast<rmw_liveliness_changed_status_t *>(event_info);
        DDS_LivelinessChangedStatus dds_status = DDS_LivelinessChangedStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataReader_get_liveliness_changed_status(this->reader, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get liveliness changed status")
          return RMW_RET_ERROR;
        }

        status->alive_count = dds_status.alive_count;
        status->alive_count_change = dds_status.alive_count_change;
        status->not_alive_count = dds_status.not_alive_count;
        status->not_alive_count_change = dds_status.not_alive_count_change;

        break;
      }
    case RMW_EVENT_REQUESTED_DEADLINE_MISSED:
      {
        rmw_requested_deadline_missed_status_t * status =
          reinterpret_cast<rmw_requested_deadline_missed_status_t *>(event_info);
        DDS_RequestedDeadlineMissedStatus dds_status =
          DDS_RequestedDeadlineMissedStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataReader_get_requested_deadline_missed_status(this->reader, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get requested deadline missed status")
          return RMW_RET_ERROR;
        }

        status->total_count = dds_status.total_count;
        status->total_count_change = dds_status.total_count_change;

        break;
      }
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
      {
        rmw_requested_qos_incompatible_event_status_t * const status =
          reinterpret_cast<rmw_requested_qos_incompatible_event_status_t *>(event_info);
        DDS_RequestedIncompatibleQosStatus dds_status =
          DDS_RequestedIncompatibleQosStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataReader_get_requested_incompatible_qos_status(this->reader, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get requested incompatible qos status")
          return RMW_RET_ERROR;
        }

        status->total_count = dds_status.total_count;
        status->total_count_change = dds_status.total_count_change;
        status->last_policy_kind =
          dds_qos_policy_to_rmw_qos_policy(dds_status.last_policy_id);
        break;
      }
// Avoid warnings caused by RMW_EVENT_MESSAGE_LOST not being one of
// the defined values for rmw_event_type_t. This #if and the one in
// the `default` case, should be removed once support for releases
// without RMW_EVENT_MESSAGE_LOST is dropped (or the value is backported).
#if RMW_CONNEXT_HAVE_MESSAGE_LOST
    case RMW_EVENT_MESSAGE_LOST:
      {
        rmw_message_lost_status_t * const status =
          reinterpret_cast<rmw_message_lost_status_t *>(event_info);
        DDS_SampleLostStatus dds_status = DDS_SampleLostStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataReader_get_sample_lost_status(this->reader, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get sample lost status")
          return RMW_RET_ERROR;
        }

        status->total_count = dds_status.total_count;
        status->total_count_change = dds_status.total_count_change;

        break;
      }
#endif /* RMW_CONNEXT_HAVE_MESSAGE_LOST */
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET(
          "unsupported subscriber qos: %d", event_type)
        RMW_CONNEXT_ASSERT(0)
        return RMW_RET_ERROR;
      }
  }

  return RMW_RET_OK;
}

rmw_ret_t
RMW_Connext_PublisherStatusCondition::get_status(
  const rmw_event_type_t event_type, void * const event_info)
{
  switch (event_type) {
    case RMW_EVENT_LIVELINESS_LOST:
      {
        rmw_liveliness_lost_status_t * status =
          reinterpret_cast<rmw_liveliness_lost_status_t *>(event_info);
        DDS_LivelinessLostStatus dds_status = DDS_LivelinessLostStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataWriter_get_liveliness_lost_status(this->writer, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get liveliness lost status")
          return RMW_RET_ERROR;
        }

        status->total_count = dds_status.total_count;
        status->total_count_change = dds_status.total_count_change;
        break;
      }
    case RMW_EVENT_OFFERED_DEADLINE_MISSED:
      {
        rmw_offered_deadline_missed_status_t * status =
          reinterpret_cast<rmw_offered_deadline_missed_status_t *>(event_info);
        DDS_OfferedDeadlineMissedStatus dds_status = DDS_OfferedDeadlineMissedStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataWriter_get_offered_deadline_missed_status(this->writer, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get offered deadline missed status")
          return RMW_RET_ERROR;
        }

        status->total_count = dds_status.total_count;
        status->total_count_change = dds_status.total_count_change;
        break;
      }
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
      {
        rmw_offered_qos_incompatible_event_status_t * const status =
          reinterpret_cast<rmw_offered_qos_incompatible_event_status_t *>(event_info);
        DDS_OfferedIncompatibleQosStatus dds_status =
          DDS_OfferedIncompatibleQosStatus_INITIALIZER;

        if (DDS_RETCODE_OK !=
          DDS_DataWriter_get_offered_incompatible_qos_status(this->writer, &dds_status))
        {
          RMW_CONNEXT_LOG_ERROR_SET("failed to get offered incompatible qos status")
          return RMW_RET_ERROR;
        }

        status->total_count = dds_status.total_count;
        status->total_count_change = dds_status.total_count_change;
        status->last_policy_kind =
          dds_qos_policy_to_rmw_qos_policy(dds_status.last_policy_id);
        break;
      }
    default:
      {
        RMW_CONNEXT_LOG_ERROR_A_SET("unsupported publisher qos: %d", event_type)
        RMW_CONNEXT_ASSERT(0)
        return RMW_RET_ERROR;
      }
  }

  return RMW_RET_OK;
}
