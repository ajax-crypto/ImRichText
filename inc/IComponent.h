#pragma once

#include "inja.h"

#include <unordered_set>
#include <string_view>

namespace obl
{
	struct Event
	{
		enum KeyModifiers
		{
			LeftCtrl = 1,
			RightCtrl = 2,
			LeftShift = 4,
			RightShift = 8,
			CapsLock = 16,
			LeftAlt = 32,
			RightAlt = 64
		};

		enum Type
		{
			LeftMouseDown,
			LeftMouseUp,
			LeftClick,
			RightMouseDown,
			RightMouseUp,
			RightClick,
			MouseMoved,
			MouseEnter,
			Hovered = MouseEnter,
			MouseLeave,
			MouseWheel,
			KeyDown,
			KeyUp,
			KeyPress,
			TextEdited,
			TextPasted,
			TextCopied,
			FocusChanged
		};

		float mouseX = -1, mouseY = -1;
		int whichKey = -1;
		int amount = 0;
		int keyModifiers = 0;
	};

	class IComponent
	{
	protected:

		enum class PostProcessState
		{
			ImmediateUpdate,
			ScheduleUpdate,
			SkipUpdate
		};

		IComponent(UISceneNode* parent) {}

		virtual std::pair<std::string_view, std::optional<nlohmann::json>> view() const = 0;

		virtual std::vector<std::pair<std::string, Event::Type>> getBindings() const {
			return {};
		}

		virtual PostProcessState processEvent(std::string_view id, Event::Type eventType) {
			return PostProcessState::SkipUpdate;
		}

		virtual void dispatchActivity(std::string_view id, Event::Type eventType) {}

		virtual std::unordered_set<IComponent*> childComponentList() const {
			return {};
		}

		std::string materializeView() const {
			auto pair = view();

			if (pair.second) {
				inja::Environment env;
				return env.render(pair.first, pair.second.value());
			}
			else {
				return pair.first.data();
			}
		}

		bool isRendered() const {
			return m_isFirstTime;
		}

	public:

		template <typename ImplT, typename... ArgsT>
		static ImplT* Register(UISceneNode* scene, ArgsT&&... args)
		{
			static_assert(std::is_base_of_v<IComponent, ImplT>);

			auto impl = new ImplT{ scene, std::forward<ArgsT>(args)... };
			UpdateView(scene, impl);
			return impl;
		}

		std::string materializeView(const nlohmann::json& context) const
		{
			auto tmpl = view().first;
			inja::Environment env;
			return env.render(tmpl, context);
		}

	private:

		template <typename ImplT>
		static void UpdateView(UISceneNode* scene, ImplT* impl)
		{
			if (!impl->m_isFirstTime)
			{
				//scene->childsCloseAll();
			}

			scene->invalidateDraw();
			scene->invalidateStyle(scene->getRoot());

			scene->loadLayoutFromString("");
			scene->invalidate(scene->getRoot());
			//scene->update();

			scene->loadLayoutFromString(impl->materializeView());
			scene->invalidate(scene->getRoot());
			RegisterBindings(scene, impl);

			impl->m_isFirstTime = false;
		}

		template <typename ImplT>
		static void RegisterBindings(UISceneNode* scene, ImplT* impl)
		{
			for (const auto& pair : impl->getBindings()) {
				auto widget = scene->find<UIWidget>(pair.first);

				if (widget) {
					widget->on(pair.second, [scene, impl, id = pair.first](const Event* ev) {
						HandleEvent(scene, impl, id, ev);
						});
				}
				else {
					Log::debug("Unable to add binding to widget %s", pair.first.c_str());
				}
			}
		}

		template <typename ImplT>
		static void HandleEvent(UISceneNode* scene, ImplT* impl, const std::string& id,
			const Event* ev)
		{
			PostProcessState state = impl->processEvent(id, (Event::EventType)ev->getType());

			switch (state) {
			case IComponent::PostProcessState::ImmediateUpdate:
				UpdateView(scene, impl);
				break;
			case IComponent::PostProcessState::ScheduleUpdate:
				/* TODO ... */
				break;
			case IComponent::PostProcessState::SkipUpdate:
				break;
			default:
				break;
			}
		}

		bool m_isFirstTime = true;
	};
}