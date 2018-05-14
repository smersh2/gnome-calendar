/* gcal-recurrence.c
 *
 * Copyright (C) 2017 Yash Singh <yashdev10p@gmail.com>
 *
 * gnome-calendar is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gnome-calendar is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gcal-recurrence.h"
#include "gcal-utils.h"
#include "gcal-event.h"

#include <glib.h>

G_DEFINE_BOXED_TYPE (GcalRecurrence, gcal_recurrence, gcal_recurrence_ref, gcal_recurrence_unref)

static void
gcal_recurrence_free (GcalRecurrence *self)
{
  g_assert (self);
  g_assert_cmpint (self->ref_count, ==, 0);

  gcal_clear_datetime (&self->limit.until);
  g_slice_free (GcalRecurrence, self);
}

/**
 * gcal_recurrence_new:
 *
 * Creates a new #GcalRecurrence
 *
 * Returns: (transfer full): a #GcalRecurrence
 */
GcalRecurrence*
gcal_recurrence_new (void)
{
  GcalRecurrence *new_recur;

  new_recur = g_slice_new (GcalRecurrence);

  new_recur->frequency = GCAL_RECURRENCE_NO_REPEAT;
  new_recur->limit_type = GCAL_RECURRENCE_FOREVER;

  new_recur->limit.until = NULL;
  new_recur->limit.count = 0;

  new_recur->ref_count = 1;

  return new_recur;
}

/**
 * gcal_recurrence_copy:
 * @recur: a #GcalRecurrence
 *
 * Creates a copy of @recur
 *
 * Returns: (transfer full): a #GcalRecurrence
 */
GcalRecurrence*
gcal_recurrence_copy (GcalRecurrence *recur)
{
  GcalRecurrence *new_recur;

  g_return_val_if_fail (recur != NULL, NULL);

  new_recur = gcal_recurrence_new ();

  new_recur->frequency = recur->frequency;
  new_recur->limit_type = recur->limit_type;

  if (recur->limit_type == GCAL_RECURRENCE_UNTIL && recur->limit.until)
    new_recur->limit.until = g_date_time_ref (recur->limit.until);

  new_recur->limit.count = recur->limit.count;

  return new_recur;
}

GcalRecurrence *
gcal_recurrence_ref (GcalRecurrence *self)
{
  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (self->ref_count, NULL);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
gcal_recurrence_unref (GcalRecurrence *self)
{
  g_return_if_fail (self);
  g_return_if_fail (self->ref_count);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    gcal_recurrence_free (self);
}

/**
 * gcal_recurrence_is_equal:
 * @recur1: (nullable): a #GcalRecurrence
 * @recur2: (nullable): a #GcalRecurrence
 *
 * Checks if @recur1 and @recur2 are equal or not
 *
 * Returns: %TRUE if both are equal, %FALSE otherwise
 */

gboolean
gcal_recurrence_is_equal (GcalRecurrence *recur1,
                          GcalRecurrence *recur2)
{
  if (recur1 == recur2)
    return TRUE;
  else if (!recur1 || !recur2)
    return FALSE;

  if (recur1->frequency != recur2->frequency)
    return FALSE;

  if (recur1->limit_type != recur2->limit_type)
    return FALSE;

  if (recur1->limit_type == GCAL_RECURRENCE_UNTIL)
    {
      if (!g_date_time_equal (recur1->limit.until, recur2->limit.until))
        return FALSE;
    }
  else if (recur1->limit_type == GCAL_RECURRENCE_COUNT)
    {
      if (recur1->limit.count != recur2->limit.count)
        return FALSE;
    }

  return TRUE;
}

/**
 * gcal_recurrence_parse_recurrence_rules:
 * @comp: an #ECalComponent
 *
 * Parses the rrule of the @comp and
 * sets the GcalRecurrence struct accordingly.
 *
 * Returns: (transfer full): a #GcalRecurrence
 */
GcalRecurrence*
gcal_recurrence_parse_recurrence_rules (ECalComponent *comp)
{
  GcalRecurrence *recur;
  icalproperty *prop;
  icalcomponent *icalcomp;
  struct icalrecurrencetype rrule;

  if (!e_cal_component_has_recurrences (comp))
    return NULL;

  recur = gcal_recurrence_new ();
  icalcomp = e_cal_component_get_icalcomponent (comp);

  prop = icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY);
  g_return_val_if_fail (prop != NULL, NULL);

  rrule = icalproperty_get_rrule (prop);

  switch (rrule.freq)
    {
      case ICAL_DAILY_RECURRENCE:
        recur->frequency = GCAL_RECURRENCE_DAILY;
        break;

      case ICAL_WEEKLY_RECURRENCE:
        {
          if (rrule.by_day[0] == ICAL_MONDAY_WEEKDAY &&
              rrule.by_day[1] == ICAL_TUESDAY_WEEKDAY &&
              rrule.by_day[2] == ICAL_WEDNESDAY_WEEKDAY &&
              rrule.by_day[3] == ICAL_THURSDAY_WEEKDAY &&
              rrule.by_day[4] == ICAL_FRIDAY_WEEKDAY &&
              rrule.by_day[5] != ICAL_SATURDAY_WEEKDAY &&
              rrule.by_day[6] != ICAL_SUNDAY_WEEKDAY)
            {
              recur->frequency = GCAL_RECURRENCE_MON_FRI;
            }
          else
            {
              recur->frequency = GCAL_RECURRENCE_WEEKLY;
            }
          break;
        }

      case ICAL_MONTHLY_RECURRENCE:
        recur->frequency = GCAL_RECURRENCE_MONTHLY;
        break;

      case ICAL_YEARLY_RECURRENCE:
        recur->frequency = GCAL_RECURRENCE_YEARLY;
        break;

      default:
        recur->frequency = GCAL_RECURRENCE_OTHER;
    }

  if (rrule.count > 0)
    {
      recur->limit_type = GCAL_RECURRENCE_COUNT;
      recur->limit.count = rrule.count;
    }
  else if (rrule.until.year != 0)
    {
      recur->limit_type = GCAL_RECURRENCE_UNTIL;
      recur->limit.until = icaltime_to_datetime (&rrule.until);
    }
  else
    {
      recur->limit_type = GCAL_RECURRENCE_FOREVER;
    }

  return recur;
}

/**
 * gcal_recurrence_to_rrule:
 * @recur: a #GcalRecurrence
 *
 * Converts @recur into corresponding rrule.
 *
 * Returns: (transfer full): a #struct icalrecurrencetype
 */
struct icalrecurrencetype*
gcal_recurrence_to_rrule (GcalRecurrence *recur)
{
  struct icalrecurrencetype *rrule;

  if (!recur)
    return NULL;

  /* Initialize and clear the rrule to get rid of unwanted fields */
  rrule = g_new0 (struct icalrecurrencetype, 1);
  icalrecurrencetype_clear (rrule);

  switch (recur->frequency)
    {
    case GCAL_RECURRENCE_DAILY:
      rrule->freq = ICAL_DAILY_RECURRENCE;
      break;

    case GCAL_RECURRENCE_WEEKLY:
      rrule->freq = ICAL_WEEKLY_RECURRENCE;
      break;

    case GCAL_RECURRENCE_MONTHLY:
      rrule->freq = ICAL_MONTHLY_RECURRENCE;
      break;

    case GCAL_RECURRENCE_YEARLY:
      rrule->freq = ICAL_YEARLY_RECURRENCE;
      break;

    case GCAL_RECURRENCE_NO_REPEAT:
      rrule->freq = ICAL_NO_RECURRENCE;
      break;

    case GCAL_RECURRENCE_MON_FRI:
      {
        rrule->freq = ICAL_WEEKLY_RECURRENCE;
        rrule->by_day[0] = ICAL_MONDAY_WEEKDAY;
        rrule->by_day[1] = ICAL_TUESDAY_WEEKDAY;
        rrule->by_day[2] = ICAL_WEDNESDAY_WEEKDAY;
        rrule->by_day[3] = ICAL_THURSDAY_WEEKDAY;
        rrule->by_day[4] = ICAL_FRIDAY_WEEKDAY;
        break;
      }

    default:
      rrule->freq = ICAL_NO_RECURRENCE;
      break;
    }

  switch (recur->limit_type)
    {
    case GCAL_RECURRENCE_COUNT:
      rrule->count = recur->limit.count;
      break;

    case GCAL_RECURRENCE_UNTIL:
      {
        rrule->until.second = g_date_time_get_second (recur->limit.until);
        rrule->until.minute = g_date_time_get_minute (recur->limit.until);
        rrule->until.hour = g_date_time_get_hour (recur->limit.until);
        rrule->until.day = g_date_time_get_day_of_month (recur->limit.until);
        rrule->until.month = g_date_time_get_month (recur->limit.until);
        rrule->until.year = g_date_time_get_year (recur->limit.until);
        break;
      }

    case GCAL_RECURRENCE_FOREVER:
      break;

    default:
      break;
    }

  return rrule;
}
