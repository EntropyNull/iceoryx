// Copyright (c) 2021 by Apex.AI Inc. All rights reserved.
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

#include "iceoryx_posh/iceoryx_posh_types.hpp"
#include "iceoryx_posh/internal/popo/building_blocks/condition_variable_data.hpp"
#include "iceoryx_posh/popo/active_call_set.hpp"
#include "iceoryx_posh/popo/user_trigger.hpp"
#include "iceoryx_utils/cxx/vector.hpp"
#include "mocks/wait_set_mock.hpp"
#include "test.hpp"
#include "testutils/timing_test.hpp"

#include <chrono>
#include <memory>
#include <thread>

using namespace ::testing;
using ::testing::Return;
using namespace iox::popo;
using namespace iox::cxx;
using namespace iox::units::duration_literals;

class ActiveCallSet_test : public Test
{
  public:
    enum SimpleEvent
    {
        StoepselBachelorParty,
        Hypnotoad
    };

    class SimpleEventClass
    {
      public:
        SimpleEventClass() = default;
        SimpleEventClass(const SimpleEventClass&) = delete;
        SimpleEventClass(SimpleEventClass&&) = delete;

        SimpleEventClass& operator=(const SimpleEventClass&) = delete;
        SimpleEventClass& operator=(SimpleEventClass&&) = delete;

        ~SimpleEventClass()
        {
        }

        void enableEvent(iox::popo::TriggerHandle&& handle, const SimpleEvent event) noexcept
        {
            if (event == SimpleEvent::StoepselBachelorParty)
            {
                m_handleStoepsel = std::move(handle);
            }
            else
            {
                m_handleHypnotoad = std::move(handle);
            }
        }

        void enableEvent(iox::popo::TriggerHandle&& handle) noexcept
        {
            m_handleHypnotoad = std::move(handle);
        }

        void invalidateTrigger(const uint64_t id)
        {
            m_invalidateTriggerId = id;
            if (m_handleHypnotoad.getUniqueId() == id)
            {
                m_handleHypnotoad.invalidate();
            }
            else if (m_handleStoepsel.getUniqueId() == id)
            {
                m_handleStoepsel.invalidate();
            }
        }

        iox::cxx::ConstMethodCallback<bool> getHasTriggeredCallbackForEvent() const noexcept
        {
            return {*this, &SimpleEventClass::hasTriggered};
        }

        bool hasTriggered() const
        {
            return m_hasTriggered.exchange(false);
        }

        void disableEvent(const SimpleEvent event)
        {
            if (event == SimpleEvent::StoepselBachelorParty)
            {
                m_handleStoepsel.reset();
            }
            else
            {
                m_handleHypnotoad.reset();
            }
        }

        void disableEvent()
        {
            m_handleHypnotoad.reset();
        }

        void triggerStoepsel()
        {
            m_hasTriggered.store(true);
            m_handleStoepsel.trigger();
        }

        void resetTrigger()
        {
            m_hasTriggered.store(false);
        }

        iox::popo::TriggerHandle m_handleHypnotoad;
        iox::popo::TriggerHandle m_handleStoepsel;
        mutable std::atomic_bool m_hasTriggered{false};
        static uint64_t m_invalidateTriggerId;

        std::array<SimpleEventClass*, iox::MAX_NUMBER_OF_EVENTS_PER_ACTIVE_CALL_SET> m_triggerCallbackArg{nullptr};
    };

    class ActiveCallSetMock : public ActiveCallSet
    {
      public:
        ActiveCallSetMock(EventVariableData* data) noexcept
            : ActiveCallSet(data)
        {
        }
    };

    EventVariableData m_eventVarData{"Maulbeerblatt"};
    ActiveCallSetMock m_sut{&m_eventVarData};

    template <uint64_t N>
    static void triggerCallback(ActiveCallSet_test::SimpleEventClass* const event)
    {
        event->m_triggerCallbackArg[N] = event;
    }

    void SetUp()
    {
        ActiveCallSet_test::SimpleEventClass::m_invalidateTriggerId = 0U;
    };

    void TearDown(){};

    using eventVector_t = iox::cxx::vector<SimpleEventClass, iox::MAX_NUMBER_OF_EVENTS_PER_WAITSET + 1>;
    eventVector_t m_simpleEvents{iox::MAX_NUMBER_OF_EVENTS_PER_WAITSET + 1};
};
uint64_t ActiveCallSet_test::SimpleEventClass::m_invalidateTriggerId = 0U;

//////////////////////////////////
// attach / detach test collection
//////////////////////////////////

TEST_F(ActiveCallSet_test, IsEmptyWhenConstructed)
{
    EXPECT_THAT(m_sut.size(), Eq(0U));
}

TEST_F(ActiveCallSet_test, AttachingWithoutEnumIfEnoughSpaceAvailableWorks)
{
    EXPECT_FALSE(m_sut.attachEvent(m_simpleEvents[0], ActiveCallSet_test::triggerCallback<0>).has_error());
    EXPECT_THAT(m_sut.size(), Eq(1U));
}

TEST_F(ActiveCallSet_test, AttachWithoutEnumTillCapacityIsFullWorks)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        EXPECT_FALSE(m_sut.attachEvent(m_simpleEvents[i], ActiveCallSet_test::triggerCallback<0>).has_error());
    }
    EXPECT_THAT(m_sut.size(), Eq(m_sut.capacity()));
}

TEST_F(ActiveCallSet_test, DetachDecreasesSize)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        m_sut.attachEvent(m_simpleEvents[i], ActiveCallSet_test::triggerCallback<0>);
    }
    m_sut.detachEvent(m_simpleEvents[0]);
    EXPECT_THAT(m_sut.size(), Eq(m_sut.capacity() - 1));
}

TEST_F(ActiveCallSet_test, AttachWithoutEnumOneMoreThanCapacityFails)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        m_sut.attachEvent(m_simpleEvents[i], ActiveCallSet_test::triggerCallback<0>).has_error();
    }
    EXPECT_TRUE(
        m_sut.attachEvent(m_simpleEvents[m_sut.capacity()], ActiveCallSet_test::triggerCallback<0>).has_error());
}

TEST_F(ActiveCallSet_test, AttachingWithEnumIfEnoughSpaceAvailableWorks)
{
    EXPECT_FALSE(m_sut
                     .attachEvent(m_simpleEvents[0],
                                  ActiveCallSet_test::SimpleEvent::Hypnotoad,
                                  ActiveCallSet_test::triggerCallback<0>)
                     .has_error());
}

TEST_F(ActiveCallSet_test, AttachWithEnumTillCapacityIsFullWorks)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        EXPECT_FALSE(m_sut
                         .attachEvent(m_simpleEvents[i],
                                      ActiveCallSet_test::SimpleEvent::Hypnotoad,
                                      ActiveCallSet_test::triggerCallback<0>)
                         .has_error());
    }
}

TEST_F(ActiveCallSet_test, AttachWithEnumOneMoreThanCapacityFails)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        m_sut
            .attachEvent(
                m_simpleEvents[i], ActiveCallSet_test::SimpleEvent::Hypnotoad, ActiveCallSet_test::triggerCallback<0>)
            .has_error();
    }
    EXPECT_TRUE(m_sut
                    .attachEvent(m_simpleEvents[m_sut.capacity()],
                                 ActiveCallSet_test::SimpleEvent::Hypnotoad,
                                 ActiveCallSet_test::triggerCallback<0>)
                    .has_error());
}

TEST_F(ActiveCallSet_test, DetachMakesSpaceForAnotherAttachWithEventEnum)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        m_sut
            .attachEvent(
                m_simpleEvents[i], ActiveCallSet_test::SimpleEvent::Hypnotoad, ActiveCallSet_test::triggerCallback<0>)
            .has_error();
    }

    m_sut.detachEvent(m_simpleEvents[0], ActiveCallSet_test::SimpleEvent::Hypnotoad);
    EXPECT_FALSE(m_sut
                     .attachEvent(m_simpleEvents[m_sut.capacity()],
                                  ActiveCallSet_test::SimpleEvent::Hypnotoad,
                                  ActiveCallSet_test::triggerCallback<0>)
                     .has_error());
}

TEST_F(ActiveCallSet_test, DetachMakesSpaceForAnotherAttachWithoutEventEnum)
{
    for (uint64_t i = 0U; i < m_sut.capacity(); ++i)
    {
        m_sut.attachEvent(m_simpleEvents[i], ActiveCallSet_test::triggerCallback<0>).has_error();
    }

    m_sut.detachEvent(m_simpleEvents[0]);
    EXPECT_FALSE(m_sut
                     .attachEvent(m_simpleEvents[m_sut.capacity()],
                                  ActiveCallSet_test::SimpleEvent::Hypnotoad,
                                  ActiveCallSet_test::triggerCallback<0>)
                     .has_error());
}

TEST_F(ActiveCallSet_test, AttachingEventWithoutEventTypeLeadsToAttachedTriggerHandle)
{
    m_sut.attachEvent(m_simpleEvents[0], ActiveCallSet_test::triggerCallback<0>);
    EXPECT_TRUE(m_simpleEvents[0].m_handleHypnotoad.isValid());
}

TEST_F(ActiveCallSet_test, AttachingEventWithEventTypeLeadsToAttachedTriggerHandle)
{
    m_sut.attachEvent(m_simpleEvents[0],
                      ActiveCallSet_test::SimpleEvent::StoepselBachelorParty,
                      ActiveCallSet_test::triggerCallback<0>);
    EXPECT_TRUE(m_simpleEvents[0].m_handleStoepsel.isValid());
}

TEST_F(ActiveCallSet_test, AttachingSameEventWithEventEnumTwiceFails)
{
    m_sut.attachEvent(m_simpleEvents[0],
                      ActiveCallSet_test::SimpleEvent::StoepselBachelorParty,
                      ActiveCallSet_test::triggerCallback<0>);

    EXPECT_TRUE(m_sut
                    .attachEvent(m_simpleEvents[0],
                                 ActiveCallSet_test::SimpleEvent::StoepselBachelorParty,
                                 ActiveCallSet_test::triggerCallback<0>)
                    .has_error());
}

TEST_F(ActiveCallSet_test, AttachingSameEventWithoutEventEnumTwiceFails)
{
    m_sut.attachEvent(m_simpleEvents[0], ActiveCallSet_test::triggerCallback<0>);

    EXPECT_TRUE(m_sut.attachEvent(m_simpleEvents[0], ActiveCallSet_test::triggerCallback<0>).has_error());
}

TEST_F(ActiveCallSet_test, AttachingSameClassWithTwoDifferentEventsWorks)
{
    m_sut.attachEvent(
        m_simpleEvents[0], ActiveCallSet_test::SimpleEvent::Hypnotoad, ActiveCallSet_test::triggerCallback<0>);

    EXPECT_FALSE(m_sut
                     .attachEvent(m_simpleEvents[0],
                                  ActiveCallSet_test::SimpleEvent::StoepselBachelorParty,
                                  ActiveCallSet_test::triggerCallback<0>)
                     .has_error());
}

TEST_F(ActiveCallSet_test, DetachingSameClassWithDifferentEventEnumChangesNothing)
{
    m_sut.attachEvent(
        m_simpleEvents[0], ActiveCallSet_test::SimpleEvent::Hypnotoad, ActiveCallSet_test::triggerCallback<0>);

    m_sut.detachEvent(m_simpleEvents[0], ActiveCallSet_test::SimpleEvent::StoepselBachelorParty);
    EXPECT_THAT(m_sut.size(), Eq(1U));
}

TEST_F(ActiveCallSet_test, DetachingDifferentClassWithSameEventEnumChangesNothing)
{
    m_sut.attachEvent(
        m_simpleEvents[0], ActiveCallSet_test::SimpleEvent::Hypnotoad, ActiveCallSet_test::triggerCallback<0>);

    m_sut.detachEvent(m_simpleEvents[1], ActiveCallSet_test::SimpleEvent::Hypnotoad);
    EXPECT_THAT(m_sut.size(), Eq(1U));
}

//
